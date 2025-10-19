"""
Initrd v2 format implementation.

Format documentation:
======================

    Image layout
    +--------------+ 0x00
    |    Header    |
    +--------------+ 0x30 (48 bytes)
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

The initrd v2 format improves on v1 with:
  - Per-entry Unix permissions, uid, gid, and mtime

------ Header ------
The header occupies the first 48 bytes of the initrd image.

    struct initrd_header_v2 {
      char signature[6];       // the signature 'I' 'N' 'I' 'T' 'v' '2'
      uint16_t flags;          // image flags
      uint32_t total_size;     // total size of the initrd image
      uint32_t data_offset;    // offset to data section
      uint16_t entry_count;    // number of entries
      uint32_t metadata_size;  // size of metadata section
      uint32_t data_size;      // size of data section
      uint32_t checksum;       // CRC32 of entire data section (0 = no checksum)
      uint32_t reserved[4];    // reserved for future use
    };
    static_assert(sizeof(struct initrd_header_v2) == 48);

Flags:
  bits 0-15: reserved

------ Metadata ------
The metadata section contains all entries.

    typedef struct initrd_entry_v2 {
      uint8_t entry_type;      // 'f'=file | 'd'=directory | 'l'=symlink
      uint8_t reserved;        // reserved
      uint16_t path_len;       // length of path
      uint16_t mode;           // unix permission bits
      uint16_t reserved2;      // reserved
      uint32_t uid;            // user id
      uint32_t gid;            // group id
      uint32_t mtime;          // modification timestamp
      uint32_t data_offset;    // offset from data section start
      uint32_t data_size;      // size of file data
      uint32_t checksum;       // CRC32 checksum of file data (0 = no checksum)
      uint32_t reserved3;      // reserved
      char path[];             // null-terminated full path
    } initrd_entry_v2_t;
    static_assert(sizeof(struct initrd_entry_v2) == 36);
    // stride = sizeof(struct initrd_v2_entry) + entry.path_len + 1

Entries are ordered by increasing path depth, with directories first,
then files, then symlinks at each level (same as v1).

------ Data ------
The data section contains file contents and symlink targets.
"""

from typing import List, Dict, Tuple, Optional
from struct import calcsize, pack, unpack
import os
import zlib

from .directive import Directive, get_intermediate_paths
from .image_v1 import sort_order_paths
from .format import PAGE_SIZE, align_up, U32_MAX


V2_HEADER_FORMAT = '<6cHLLHxxLLLLLLL'
V2_HEADER_SIZE = calcsize(V2_HEADER_FORMAT)
assert V2_HEADER_SIZE == 48

V2_ENTRY_FORMAT = '<cxHHHLLLLLLL'
V2_ENTRY_SIZE = calcsize(V2_ENTRY_FORMAT)
assert V2_ENTRY_SIZE == 36


def format_cstring(s: str) -> bytes:
    """Format a string as a null-terminated C string."""
    return s.encode('ascii') + b'\x00'


class InitrdV2Entry:
    """Represents an entry in a v2 initrd image."""

    def __init__(self, entry_type: str, path: str,
                 mode: int, uid: int, gid: int, mtime: int,
                 data_offset: int, data_size: int, checksum: int):
        self.entry_type = entry_type
        self.path = path
        self.mode = mode
        self.uid = uid
        self.gid = gid
        self.mtime = mtime
        self.data_offset = data_offset
        self.data_size = data_size
        self.checksum = checksum

    def __repr__(self) -> str:
        return f'InitrdV2Entry(type={self.entry_type}, path={self.path})'


class InitrdV2Image:
    """Handler for v2 initrd images."""

    signature: str
    flags: int
    total_size: int
    data_offset: int
    entry_count: int
    metadata_size: int
    data_size: int
    checksum: int
    entries: List[InitrdV2Entry]

    def __init__(self):
        self.signature = 'INITv2'
        self.flags = 0
        self.total_size = 0
        self.data_offset = 0
        self.entry_count = 0
        self.metadata_size = 0
        self.data_size = 0
        self.checksum = 0
        self.entries = []

    @staticmethod
    def load(filename: str, verify_checksum: bool = True) -> 'InitrdV2Image':
        img = InitrdV2Image()

        with open(filename, 'rb') as f:
            header_data = f.read(V2_HEADER_SIZE)
            if len(header_data) < V2_HEADER_SIZE:
                raise ValueError('file too small to be a valid initrd image')

            sig_bytes = unpack(V2_HEADER_FORMAT, header_data)
            img.signature = ''.join(b.decode('ascii') for b in sig_bytes[:6])
            img.flags = sig_bytes[6]
            img.total_size = sig_bytes[7]
            img.data_offset = sig_bytes[8]
            img.entry_count = sig_bytes[9]
            img.metadata_size = sig_bytes[10]
            img.data_size = sig_bytes[11]
            img.checksum = sig_bytes[12]
            # sig_bytes[13:17] are reserved fields

            if img.signature != 'INITv2':
                raise ValueError(f'not a v2 initrd image: {img.signature}')

            metadata_blob = f.read(img.metadata_size if img.metadata_size > 0 else img.data_offset - V2_HEADER_SIZE)

            offset = 0
            for _ in range(img.entry_count):
                if offset + V2_ENTRY_SIZE > len(metadata_blob):
                    raise ValueError('truncated metadata')

                entry_data = metadata_blob[offset:offset + V2_ENTRY_SIZE]
                entry_type, path_len, mode, _, uid, gid, mtime, data_off, data_sz, chksum, _ = unpack(
                    V2_ENTRY_FORMAT, entry_data)

                entry_type = entry_type.decode('ascii')
                offset += V2_ENTRY_SIZE

                if offset + path_len + 1 > len(metadata_blob):
                    raise ValueError('truncated path string')

                path = metadata_blob[offset:offset + path_len].decode('ascii')
                offset += path_len + 1

                img.entries.append(InitrdV2Entry(entry_type, path,
                                                 mode, uid, gid, mtime,
                                                 data_off, data_sz, chksum))

            if verify_checksum and img.checksum != 0:
                f.seek(img.data_offset)
                data_blob = f.read(img.data_size)
                actual_checksum = zlib.crc32(data_blob) & 0xFFFFFFFF
                if actual_checksum != img.checksum:
                    raise ValueError(
                        f'data section checksum mismatch: '
                        f'expected {img.checksum:08x}, got {actual_checksum:08x}'
                    )

        return img

    def save(self, filename: str, directives: List[Directive]):
        paths: Dict[str, Tuple[Directive, int, int]] = {}
        files: Dict[str, Tuple[int, int]] = {}  # operand -> (offset, checksum)

        # first pass: collect all paths and dedup file data
        for d in directives:
            if d.path in paths:
                continue

            # add intermediate directories
            for p in get_intermediate_paths(d.path):
                if p not in paths:
                    new_dir = Directive('d', p, None, mode=d.mode, uid=d.uid, gid=d.gid, mtime=d.mtime)
                    paths[p] = (new_dir, 0, 0)

            paths[d.path] = (d, 0, 0)

        # sort paths by depth, type, then name (like v1)
        path_list = sort_order_paths(paths)

        # second pass: calculate data offsets and checksums
        curr_data_offset = 0
        updated_paths: Dict[str, Tuple[Directive, int, int]] = {}

        for path in path_list:
            d, _, _ = paths[path]
            data_offset = 0
            checksum = 0

            if d.isfile():
                # check if we've already stored this file
                if d.operand in files:
                    data_offset, checksum = files[d.operand]
                else:
                    data_offset = curr_data_offset
                    with open(d.operand, 'rb') as f:
                        data = f.read()
                    checksum = zlib.crc32(data) & 0xFFFFFFFF
                    curr_data_offset += align_up(d.size, PAGE_SIZE)
                    files[d.operand] = (data_offset, checksum)
            elif d.islink():
                data_offset = curr_data_offset
                data = format_cstring(d.operand)
                checksum = zlib.crc32(data) & 0xFFFFFFFF
                curr_data_offset += align_up(d.size, PAGE_SIZE)

            updated_paths[path] = (d, data_offset, checksum)

        paths = updated_paths

        # build metadata section
        metadata_blob = bytearray()
        for path in path_list:
            d, data_offset, checksum = paths[path]

            path_bytes = path.encode('ascii')
            path_len = len(path_bytes)

            entry_data = pack(V2_ENTRY_FORMAT,
                              d.kind.encode('ascii'),
                              path_len,
                              d.mode,
                              0,  # reserved2
                              d.uid,
                              d.gid,
                              d.mtime,
                              data_offset,
                              d.size,
                              checksum,
                              0)  # reserved3

            metadata_blob.extend(entry_data)
            metadata_blob.extend(path_bytes)
            metadata_blob.extend(b'\x00')

        metadata_blob = bytes(metadata_blob)

        flags = 0
        metadata_size = len(metadata_blob)
        data_start_offset = align_up(V2_HEADER_SIZE + metadata_size, PAGE_SIZE)
        data_size = curr_data_offset
        total_size = data_start_offset + data_size

        # build data section in memory to calculate checksum
        data_blob = bytearray()
        written_files = set()
        for path in path_list:
            d, offset, chksum = paths[path]
            if d.isfile():
                if d.operand in written_files:
                    continue
                written_files.add(d.operand)

                with open(d.operand, 'rb') as src:
                    data = src.read()
                data_blob.extend(data)
                data_blob.extend(b'\x00' * (align_up(len(data), PAGE_SIZE) - len(data)))
            elif d.islink():
                data = format_cstring(d.operand)
                data_blob.extend(data)
                data_blob.extend(b'\x00' * (align_up(len(data), PAGE_SIZE) - len(data)))

        data_blob = bytes(data_blob)
        data_checksum = zlib.crc32(data_blob) & 0xFFFFFFFF

        # write the image
        with open(filename, 'wb') as f:
            sig = [b.encode('ascii') for b in self.signature[:6]]
            f.write(pack(V2_HEADER_FORMAT, *sig, flags, total_size, data_start_offset,
                         len(path_list), metadata_size, data_size, data_checksum, 0, 0, 0, 0))

            f.write(metadata_blob)
            f.write(b'\x00' * (data_start_offset - f.tell()))

            # write data section
            f.write(data_blob)

        self.flags = flags
        self.total_size = total_size
        self.data_offset = data_start_offset
        self.entry_count = len(path_list)
        self.metadata_size = metadata_size
        self.data_size = data_size
        self.checksum = data_checksum

    def get_full_path(self, index: int) -> str:
        if index >= len(self.entries):
            raise ValueError(f'invalid entry index: {index}')
        return self.entries[index].path

    def read_file_data(
        self, filename: str, entry: InitrdV2Entry, verify_checksum: bool = True
    ) -> bytes:
        if entry.data_size == 0:
            return b''

        with open(filename, 'rb') as f:
            f.seek(self.data_offset + entry.data_offset)
            data = f.read(entry.data_size)

        if verify_checksum and entry.checksum != 0:
            actual_checksum = zlib.crc32(data) & 0xFFFFFFFF
            if actual_checksum != entry.checksum:
                raise ValueError(
                    f'checksum mismatch for {entry.path}: '
                    f'expected {entry.checksum:08x}, got {actual_checksum:08x}'
                )
        return data

    def find_entry(self, path: str) -> Optional[Tuple[int, InitrdV2Entry]]:
        for idx, entry in enumerate(self.entries):
            if entry.path == path:
                return idx, entry
        return None

    def to_directives(self, filename: str) -> List[Directive]:
        import tempfile
        directives = []

        for idx, entry in enumerate(self.entries):
            full_path = self.get_full_path(idx)

            if entry.entry_type == 'd':
                d = Directive('d', full_path, None, mode=entry.mode, uid=entry.uid,
                              gid=entry.gid, mtime=entry.mtime)
                directives.append(d)
            elif entry.entry_type == 'f':
                data = self.read_file_data(filename, entry)
                with tempfile.NamedTemporaryFile(delete=False, mode='wb') as tmp:
                    tmp.write(data)
                    tmp_path = tmp.name
                d = Directive('f', full_path, tmp_path, mode=entry.mode, uid=entry.uid,
                              gid=entry.gid, mtime=entry.mtime)
                directives.append(d)
            elif entry.entry_type == 'l':
                data = self.read_file_data(filename, entry)
                target = data.rstrip(b'\x00').decode('ascii')
                d = Directive('l', full_path, target, mode=entry.mode, uid=entry.uid,
                              gid=entry.gid, mtime=entry.mtime)
                directives.append(d)

        return directives
