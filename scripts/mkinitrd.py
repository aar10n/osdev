#!/usr/bin/env python
from typing import Optional, Dict, List, Set, Tuple
from struct import calcsize, pack
import posixpath
import argparse
import os
import sys


# Initial Ramdisk Format
# ======================
#
#     Image layout
#     +--------------+ 0x00
#     |    Header    |
#     +--------------+ 0x20
#     |              |
#     |   Metadata   |
#     |              |
#     +--------------+ data_offset
#     |              |
#     |              |
#     |     Data     |
#     |              |
#     |              |
#     +--------------+
#
# The initrd format is designed to be very simple and read-only.
# There is no internal concept of hierarchy, instead there is a
# single flat list of file paths, each representing one of the
# following entry types:
#   - file        a regular file with data
#   - link        a symlink to another path
#   - directory   an empty directory
#
# Entries are ordered by increasing path depth meaning parent
# directories will appear before their children. Additionally,
# entries are arranged such that all children of a given directory
# will appear immediately after their parent, with regular files
# appearing before symlinks followed by directories last. The root
# directory '/' is implied.
#
# For example, if the following paths are added to the initrd:
#   - /home/user/.bashrc
#   - /home/user/.config/
#   - /usr/include/stdio.h
#   - /usr/include/sys/types.h
#   - /usr/bin/sed
#
# This results in the following path order:
#   - /home
#   - /home/user
#   - /home/user/.bashrc
#   - /home/user/.config/
#   - /usr
#   - /usr/bin
#   - /usr/bin/sed
#   - /usr/include
#   - /usr/include/stdio.h
#   - /usr/include/sys
#   - /usr/include/sys/types.h
#
# The limitations of the initrd format are as follows:
#   - max total image size of 4GB
#   - max number of entries at 65535
#
# ------ Header ------
# The header occupies the first 32 bytes of the initrd image. The
# flags field is currently not used.
#
#     struct initrd_header {
#       char signature[6];       // the signature 'I' 'N' 'I' 'T' 'v' '1'
#       uint16_t flags;          // initrd flags
#       uint32_t total_size;     // total size of the initrd image
#       uint32_t data_offset;    // offset from start of image to start of data section
#       uint16_t entry_count;    // number of entries in the metadata section
#       uint8_t reserved[14];    // reserved
#     };
#     // sizeof(struct initrd_header) == 32
#
# ------ Metadata ------
# The metadata section follows immediately after the header and
# consists of a number of metadata entries. Each entry contains
# some metadata and a variable length, non-null terminated path
# string. Each entry has the following format:
#
#     struct initrd_entry {
#       uint8_t entry_type;   // type: 'f'=file | 'd'=directory | 'l'=symlink
#       uint8_t reserved;     // reserved
#       uint16_t path_len;    // length of the file path
#       uint32_t data_offset; // offset from start of image to associated data
#       uint32_t data_size;   // size of the associated data
#       char path[];          // file path
#     }
#     // sizeof(struct initrd_entry) == 12
#     // stride = sizeof(struct initrd_entry) + entry.path_len + 1
#
# Both 'f' (file) and 'l' (link) type entries have a non-zero
# data_size value and a valid data_offset value. For 'f' (file)
# entries, the data is the full raw file data. For 'l' (link)
# entries, the data is a null-terminated path string. 'd' (directory)
# type entries have no data.
#
# ------ Data ------
# The data section starts at the image offset given by the header
# field `data_offset`.
#

U16_MAX = 0xFFFF
U32_MAX = 0xFFFFFFFF
PAGE_SIZE = 0x1000

HEADER_FORMAT = '<6cHLLH14x'
HEADER_SIZE = calcsize(HEADER_FORMAT)
assert HEADER_SIZE == 32

ENTRY_FORMAT = '<cxHLL'
ENTRY_SIZE = calcsize(ENTRY_FORMAT)
assert ENTRY_SIZE == 12


def align_up(_n: int, _align: int) -> int:
    return (_n + _align - 1) & ~(_align - 1)


def format_cstring(s: str) -> bytes:
    return s.encode('ascii') + bytes('\0', 'ascii')


def format_header(_signature: str, _flags: int, _total_size: int, _data_offset: int, _entry_count: int) -> bytes:
    assert 0 <= _flags < U16_MAX
    assert 0 <= _total_size < U32_MAX
    assert 0 <= _data_offset < U32_MAX
    assert 0 < _entry_count < U16_MAX
    sig = [b.encode('ascii') for b in _signature[:6]]
    return pack(HEADER_FORMAT, *sig, _flags, _total_size, _data_offset, _entry_count)


def format_entry(_kind: str, _data_offset: int, _data_size, _path: str) -> bytes:
    assert _kind in ['f', 'd', 'l']
    assert 0 <= _data_offset < U32_MAX
    assert 0 <= _data_size < U32_MAX
    assert 0 < len(_path) < U16_MAX
    _kind = _kind.encode('ascii')
    return pack(ENTRY_FORMAT, _kind, len(_path), _data_offset, _data_size) + format_cstring(_path)


class CustomHelpFormatter(argparse.HelpFormatter):
    def _fill_text(self, text, width, indent):
        return ''.join(indent + line for line in text.splitlines(keepends=True))

    def _split_lines(self, text, width):
        return text.splitlines()

    def _format_action_invocation(self, action):
        if not action.option_strings or action.nargs == 0:
            return super()._format_action_invocation(action)
        default = self._get_default_metavar_for_optional(action)
        args_string = self._format_args(action, default)
        return ', '.join(action.option_strings) + ' ' + args_string


class Directive:
    def __init__(self, kind: str, path: str, operand: Optional[str] = None):
        self.kind = kind
        self.path = path
        self.operand = operand
        self.size = 0

        if kind == 'f':
            assert operand is not None
            try:
                if not os.path.isfile(operand):
                    raise argparse.ArgumentTypeError(f'path is not a file: {operand}')
                self.size = os.path.getsize(operand)
            except FileNotFoundError:
                raise argparse.ArgumentTypeError(f'file not found: {operand}')
            except Exception as e:
                raise argparse.ArgumentTypeError(f'{e}')
        elif kind == 'l':
            assert operand is not None
            self.size = len(operand) + 1
        elif kind != 'd':
            raise ValueError(f'invalid directive kind: {kind}')

    def isfile(self) -> bool:
        return self.kind == 'f'

    def islink(self) -> bool:
        return self.kind == 'l'

    def isdir(self) -> bool:
        return self.kind == 'd'

    def aligned_size(self) -> int:
        return align_up(self.size, PAGE_SIZE)

    def __repr__(self) -> str:
        return f'Directive(kind=\'{self.kind}\', path={self.path}, operand={self.operand}, size={self.size})'


def parse_directive(s: str) -> Directive:
    if ':' not in s:
        raise argparse.ArgumentTypeError(f'invalid directive: {s}')

    if s[0] == ':':
        # :<path>/|directory
        kind = 'd'
        path = posixpath.normpath(s[1:]) + '/'
        operand = None
    elif s[0] == 'l':
        # l<target>:<path>|symlink
        kind = 'l'
        path = s[s.index(':') + 1:]
        operand = s[1:s.index(':')]
    else:
        # <srcfile>:<path>|file
        kind = 'f'
        path = s[s.index(':') + 1:]
        operand = s[:s.index(':')]
        if not os.path.isfile(operand):
            raise argparse.ArgumentTypeError(f'path is not a file: {operand}')

    if not posixpath.isabs(path):
        raise argparse.ArgumentTypeError(f'invalid path: {path}')
    return Directive(kind, path, operand)


def get_intermediate_paths(path: str) -> List[str]:
    _parts = []
    if path == '/':
        return _parts
    if path.endswith('/'):
        path = path[:-1]

    while True:
        dirname, _ = posixpath.split(path)
        if dirname != '.' and dirname != '/':
            _parts += [dirname + '/']
        else:
            break
        path = dirname
    return list(reversed(_parts))


def sort_order_paths(_paths: Dict[str, Tuple[Directive, int]]) -> List[str]:
    # First, create a list of all paths with their parent directories
    all_entries = []  # type: List[Tuple[int, int, str]]  # (depth, type_priority, path)
    
    for _path, (directive, _) in _paths.items():
        depth = _path.count(os.sep)
        # Sort order: directories first (0), then files (1), then links (2)
        if directive.isdir():
            type_priority = 0
        elif directive.isfile():
            type_priority = 1
        else:  # link
            type_priority = 2
        all_entries.append((depth, type_priority, _path))
    
    # Sort by depth first (shallowest first), then by type (directories first), then alphabetically
    all_entries.sort(key=lambda x: (x[0], x[1], x[2].lower()))
    
    # Extract just the paths
    return [path for _, _, path in all_entries]


#
# Main
#

MAGIC = 'INITv1'

usage_text = f"""\
%(prog)s -o <file> [-f <file>] <directive>...
"""

epilog_text = """
directives:
    :<path>/          create an empty directory at <path>
    l<target>:<path>  create a symlink to <target> at <path>
    <srcfile>:<path>  add <srcfile> to the image at <path>
"""

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        usage=usage_text,
        description='Generate an initrd filesystem image.',
        epilog=epilog_text,
        formatter_class=CustomHelpFormatter
    )
    parser.add_argument('-o', '--outfile', required=True, metavar='<file>', dest='outfile',
                        help="write the image to <file>")
    parser.add_argument('-f', '--file', action='append', metavar='<file>', dest='infiles', default=[],
                        help="read directives from <file>")
    parser.add_argument('directives', nargs='*', help=argparse.SUPPRESS)
    args = parser.parse_args()

    output_file = args.outfile
    directives = []
    for filename in args.infiles:
        try:
            with open(filename, 'r') as f:
                for line in f.read().splitlines():
                    line = line.strip()
                    if line and not line.startswith('#'):
                        directives += [parse_directive(line)]
        except FileNotFoundError:
            parser.error(f'file not found: {filename}')
        except Exception as e:
            parser.error(f'{e}')
    # put these at the end
    directives += [parse_directive(s) for s in args.directives]

    paths = dict()   # type: Dict[str, (Directive, int)] # image path -> (directive, data_offset)
    files = dict()   # type: Dict[str, int] # source file -> data_offset
    alldirs = set()  # type: Set[str] # all directories in the image
    metadata_size = 0
    curr_data_offset = 0

    ##
    # first pass: collect information
    for d in directives:
        if d.path in paths:
            sys.stderr.write(f'warning: skipping duplicate path: {d.path}\n')
            continue

        # add missing intermediate directories
        for p in get_intermediate_paths(d.path):
            if p not in paths:
                paths[p] = (Directive('d', p, None), 0)
                alldirs.add(p)

        offset = 0
        if d.isfile():
            if d.operand in files:
                offset = files[d.operand]
            else:
                offset = curr_data_offset
                files[d.operand] = offset
                curr_data_offset += d.aligned_size()
        elif d.islink():
            # TODO: pack symlink data
            offset = curr_data_offset
            curr_data_offset += d.aligned_size()

        paths[d.path] = (d, offset)

    # sort paths
    path_list = sort_order_paths(paths)

    # recalculate data offsets in sorted order
    curr_data_offset = 0
    for p in path_list:
        d, old_offset = paths[p]
        if d.isfile() or d.islink():
            paths[p] = (d, curr_data_offset)
            curr_data_offset += d.aligned_size()

    # calculate metadata size
    for p in path_list:
        metadata_size += ENTRY_SIZE + len(p) + 1

    ##
    # second pass: write the image
    entry_count = len(path_list)
    data_size = curr_data_offset
    data_start_offset = align_up(HEADER_SIZE + metadata_size, PAGE_SIZE)
    total_size = data_start_offset + data_size

    with open(output_file, 'wb') as f:
        # write header
        f.write(format_header(MAGIC, 0, total_size, data_start_offset, entry_count))

        # write metadata
        for p in path_list:
            d, offset = paths[p]
            print(f'adding {d.kind} {d.path}')
            f.write(format_entry(d.kind, data_start_offset+offset, d.size, p))

        # pad to page size
        f.write(b'\0' * (data_start_offset - f.tell()))

        # write data in the same order as metadata
        for p in path_list:
            d, offset = paths[p]
            if d.isfile():
                with open(d.operand, 'rb') as src:
                    f.write(src.read())
                    f.write(b'\0' * (d.aligned_size() - d.size))
            elif d.islink():
                f.write(format_cstring(d.operand))
                f.write(b'\0' * (d.aligned_size() - d.size))

        print(f'wrote {total_size} bytes to {output_file}')
        print(f'  entry count: {entry_count}')
        print(f'  data offset: {data_start_offset}')
        print(f'  data size: {data_size}')
