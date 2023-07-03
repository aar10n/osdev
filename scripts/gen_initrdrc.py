#!/usr/bin/env python
from typing import List
import argparse
import select
import sys
import os

# initrd directives:
#   :<path>/           - create directory at path
#   l<target>:<path>   - create a symlink to <target> at <path>
#   <srcfile>:<path>   - copy srcfile to path


def directives_from_folder(destbase: str, srcdir: str, recursive: bool = True, sysroot: str = '') -> List[str]:
    children = os.listdir(srcdir)
    _directives = []
    for child in children:
        if child in ['.DS_Store', '.git']:
            continue

        childsrc = os.path.join(srcdir, child)
        childdest = os.path.join(destbase, child)

        if os.path.isdir(childsrc):
            _directives += [f':{childdest}/']
            if recursive:
                _directives += directives_from_folder(childdest, childsrc, recursive=recursive, sysroot=sysroot)
        elif os.path.islink(childsrc):
            target = os.readlink(childsrc)
            if os.path.isfile(target) and sysroot and target.startswith(sysroot):
                # make link absolute w.r.t. sysroot
                _directives += [f'l{target.removeprefix(sysroot)}:{childdest}']
            else:
                _directives += [f'l{target}:{childdest}']
        elif os.path.isfile(childsrc):
            _directives += [f'{childsrc}:{childdest}']

    return _directives


def unchecked_directives_from_stdin() -> List[str]:
    if not select.select([sys.stdin, ], [], [], 0.0)[0]:
        return []

    return [line.rstrip() for line in sys.stdin]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(usage='%(prog)s <file> [flags] [-d dir:dest ...] [-f file:dest ...]\n',
                                     description='Generates directives for building initrd images.')
    parser.add_argument('file', help=argparse.SUPPRESS)

    parser.add_argument('-a', action='store_true', default=False,help='Append to the file instead of writing to it')
    parser.add_argument('-R', action='store_true', default=False, help='Do not recurse into directories')
    parser.add_argument('-n', action='store_true', default=False,
                        help='Print the directives to stdout but do not write to the file')
    parser.add_argument('-S', metavar='sysroot', help='Use `sysroot` as the root path when handling symlinks')

    parser.add_argument('-d', action='append', default=[], metavar='dir:dest',
                        help='Add directives for all files in `dir` into `dest`')
    parser.add_argument('-f', action='append', default=[], metavar='file:dest',
                        help='Add directives for `file` as `dest`')
    args = parser.parse_args()

    directives = []

    # handle directories
    for arg in args.d:
        if ':' not in arg:
            parser.error(f'invalid format for -d: {arg}')

        srcpath, destpath = arg.split(':')
        if not destpath.endswith('/'):
            destpath += '/'

        if not os.path.isdir(srcpath):
            parser.error(f'not a directory: {srcpath}')
        if not os.path.isabs(destpath):
            parser.error(f'dest path must be absolute: {destpath}')

        directives += directives_from_folder(destpath, srcpath, recursive=not args.R,
                                             sysroot=os.path.abspath(args.S) if args.S else '')

    # handle files
    for arg in args.f:
        if ':' not in arg:
            parser.error(f'invalid format for -f: {arg}')

        srcpath, destpath = arg.split(':')
        if not os.path.isabs(destpath):
            parser.error(f'dest path must be absolute: {destpath}')
        if not os.path.isfile(srcpath):
            parser.error(f'not a file: {srcpath}')

        directives += [f'{srcpath}:{destpath}']

    # pass optional stdin
    directives += unchecked_directives_from_stdin()
    if not directives:
        parser.error('no directives specified')

    if args.n:
        print('\n'.join(directives))
        sys.exit(0)

    mode = 'a' if args.a else 'w'
    with open(args.file, mode) as f:
        f.write('\n'.join(directives) + '\n')
