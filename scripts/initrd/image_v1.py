"""
Initrd v1 format implementation.

Format documentation:
======================

    Image layout
    +--------------+ 0x00
    |    Header    |
    +--------------+ 0x20
    |              |
    |   Metadata   |
    |              |
    +--------------+ data_offset
    |              |
    |              |
    |     Data     |
    |              |
    |              |
    +--------------+

The initrd v1 format is designed to be very simple and read-only.
There is no internal concept of hierarchy, instead there is a
single flat list of file paths, each representing one of the
following entry types:
  - file        a regular file with data
  - link        a symlink to another path
  - directory   an empty directory

Entries are ordered by increasing path depth meaning parent
directories will appear before their children. Additionally,
entries are arranged such that all children of a given directory
will appear immediately after their parent, with directories first,
then files, then symlinks.

------ Header ------
The header occupies the first 32 bytes of the initrd image.

    struct initrd_header {
      char signature[6];       // the signature 'I' 'N' 'I' 'T' 'v' '1'
      uint16_t flags;          // initrd flags (unused in v1)
      uint32_t total_size;     // total size of the initrd image
      uint32_t data_offset;    // offset from start of image to start of data section
      uint16_t entry_count;    // number of entries in the metadata section
      uint8_t reserved[14];    // reserved
    };
    static_assert(sizeof(struct initrd_header) == 32);

------ Metadata ------
The metadata section follows immediately after the header and
consists of a number of metadata entries.

    struct initrd_entry {
      uint8_t entry_type;   // type: 'f'=file | 'd'=directory | 'l'=symlink
      uint8_t reserved;     // reserved
      uint16_t path_len;    // length of the file path
      uint32_t data_offset; // offset from start of image to associated data
      uint32_t data_size;   // size of the associated data
      char path[];          // file path (null-terminated)
    }

------ Data ------
The data section starts at the image offset given by the header
field `data_offset`.
"""

from typing import List, Dict, Tuple, Optional
from struct import calcsize, pack, unpack
import os

from .directive import Directive, get_intermediate_paths
from .format import PAGE_SIZE, align_up, U16_MAX, U32_MAX


V1_HEADER_FORMAT = '<6cHLLH14x'
V1_HEADER_SIZE = calcsize(V1_HEADER_FORMAT)
assert V1_HEADER_SIZE == 32

V1_ENTRY_FORMAT = '<cxHLL'
V1_ENTRY_SIZE = calcsize(V1_ENTRY_FORMAT)
assert V1_ENTRY_SIZE == 12


def format_cstring(s: str) -> bytes:
    return s.encode('ascii') + b'\x00'


def sort_order_paths(paths: Dict[str, Tuple]) -> List[str]:
    """Sort paths: by depth, then type (dir/file/link), then alphabetically."""
    all_entries = []

    for path, value_tuple in paths.items():
        directive = value_tuple[0]  # first element is always the Directive
        depth = path.count(os.sep)
        if directive.isdir():
            type_priority = 0
        elif directive.isfile():
            type_priority = 1
        else:
            type_priority = 2
        all_entries.append((depth, type_priority, path))

    all_entries.sort(key=lambda x: (x[0], x[1], x[2].lower()))
    return [path for _, _, path in all_entries]


class InitrdV1Entry:
    """Represents an entry in a v1 initrd image."""

    def __init__(self, entry_type: str, path: str, data_offset: int, data_size: int):
        self.entry_type = entry_type
        self.path = path
        self.data_offset = data_offset
        self.data_size = data_size

    def __repr__(self) -> str:
        return f'InitrdV1Entry(type={self.entry_type}, path={self.path}, offset={self.data_offset}, size={self.data_size})'


class InitrdV1Image:
    """Handler for v1 initrd images."""

    signature: str
    flags: int
    total_size: int
    data_offset: int
    entry_count: int
    entries: List[InitrdV1Entry]

    def __init__(self):
        self.signature = 'INITv1'
        self.flags = 0
        self.total_size = 0
        self.data_offset = 0
        self.entry_count = 0
        self.entries = []

    @staticmethod
    def load(filename: str) -> 'InitrdV1Image':
        img = InitrdV1Image()

        with open(filename, 'rb') as f:
            header_data = f.read(V1_HEADER_SIZE)
            if len(header_data) < V1_HEADER_SIZE:
                raise ValueError('file too small to be a valid initrd image')

            sig_bytes = unpack(V1_HEADER_FORMAT, header_data)
            img.signature = ''.join(b.decode('ascii') for b in sig_bytes[:6])
            img.flags = sig_bytes[6]
            img.total_size = sig_bytes[7]
            img.data_offset = sig_bytes[8]
            img.entry_count = sig_bytes[9]

            if img.signature != 'INITv1':
                raise ValueError(f'not a v1 initrd image: {img.signature}')

            for _ in range(img.entry_count):
                entry_data = f.read(V1_ENTRY_SIZE)
                if len(entry_data) < V1_ENTRY_SIZE:
                    raise ValueError('truncated entry metadata')

                entry_type, path_len, data_off, data_sz = unpack(V1_ENTRY_FORMAT, entry_data)
                entry_type = entry_type.decode('ascii')

                path_data = f.read(path_len + 1)
                if len(path_data) < path_len + 1:
                    raise ValueError('truncated path string')

                path = path_data[:path_len].decode('ascii')
                img.entries.append(InitrdV1Entry(entry_type, path, data_off, data_sz))

        return img

    def save(self, filename: str, directives: List[Directive]):
        paths: Dict[str, Tuple[Directive, int]] = {}
        files: Dict[str, int] = {}
        curr_data_offset = 0

        # first pass: collect information
        for d in directives:
            if d.path in paths:
                continue

            for p in get_intermediate_paths(d.path):
                if p not in paths:
                    paths[p] = (Directive('d', p, None), 0)

            offset = 0
            if d.isfile():
                if d.operand in files:
                    offset = files[d.operand]
                else:
                    offset = curr_data_offset
                    files[d.operand] = offset
                    curr_data_offset += align_up(d.size, PAGE_SIZE)
            elif d.islink():
                offset = curr_data_offset
                curr_data_offset += align_up(d.size, PAGE_SIZE)

            paths[d.path] = (d, offset)

        path_list = sort_order_paths(paths)

        curr_data_offset = 0
        for p in path_list:
            d, old_offset = paths[p]
            if d.isfile() or d.islink():
                paths[p] = (d, curr_data_offset)
                curr_data_offset += align_up(d.size, PAGE_SIZE)

        metadata_size = 0
        for p in path_list:
            metadata_size += V1_ENTRY_SIZE + len(p) + 1

        entry_count = len(path_list)
        data_size = curr_data_offset
        data_start_offset = align_up(V1_HEADER_SIZE + metadata_size, PAGE_SIZE)
        total_size = data_start_offset + data_size

        with open(filename, 'wb') as f:
            sig = [b.encode('ascii') for b in self.signature[:6]]
            f.write(pack(V1_HEADER_FORMAT, *sig, 0, total_size, data_start_offset, entry_count))

            for p in path_list:
                d, offset = paths[p]
                kind = d.kind.encode('ascii')
                f.write(pack(V1_ENTRY_FORMAT, kind, len(p), data_start_offset + offset, d.size))
                f.write(format_cstring(p))

            f.write(b'\x00' * (data_start_offset - f.tell()))

            for p in path_list:
                d, offset = paths[p]
                if d.isfile():
                    with open(d.operand, 'rb') as src:
                        data = src.read()
                        f.write(data)
                        f.write(b'\x00' * (align_up(d.size, PAGE_SIZE) - d.size))
                elif d.islink():
                    f.write(format_cstring(d.operand))
                    f.write(b'\x00' * (align_up(d.size, PAGE_SIZE) - d.size))

        self.total_size = total_size
        self.data_offset = data_start_offset
        self.entry_count = entry_count

    def find_entry(self, path: str) -> Optional[InitrdV1Entry]:
        for entry in self.entries:
            if entry.path == path:
                return entry
        return None

    @staticmethod
    def read_file_data(image_path: str, entry: InitrdV1Entry) -> bytes:
        if entry.data_size == 0:
            return b''

        with open(image_path, 'rb') as f:
            f.seek(entry.data_offset)
            return f.read(entry.data_size)

    def to_directives(self, filename: str) -> List[Directive]:
        import tempfile
        directives = []

        for entry in self.entries:
            if entry.entry_type == 'd':
                directives.append(Directive('d', entry.path, None))
            elif entry.entry_type == 'f':
                data = InitrdV1Image.read_file_data(filename, entry)
                with tempfile.NamedTemporaryFile(delete=False, mode='wb') as tmp:
                    tmp.write(data)
                    tmp_path = tmp.name
                directives.append(Directive('f', entry.path, tmp_path))
            elif entry.entry_type == 'l':
                data = InitrdV1Image.read_file_data(filename, entry)
                target = data.rstrip(b'\x00').decode('ascii')
                directives.append(Directive('l', entry.path, target))

        return directives
