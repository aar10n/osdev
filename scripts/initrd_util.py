#!/usr/bin/env python
"""
Utility for examining and modifying initrd filesystem images.

Supports both v1 and v2 initrd formats.
"""

from typing import List, Optional
import argparse
import posixpath
import sys
import os

from initrd import InitrdImage, Directive
from initrd.directive import parse_directive


def format_size(size: int) -> str:
    """Format size in human readable format."""
    if size < 1024:
        return f'{size}B'
    elif size < 1024 * 1024:
        return f'{size / 1024:.1f}K'
    elif size < 1024 * 1024 * 1024:
        return f'{size / (1024 * 1024):.1f}M'
    else:
        return f'{size / (1024 * 1024 * 1024):.1f}G'


def cmd_ls(image: InitrdImage, args) -> int:
    """List files in the initrd image."""
    prefix = args.prefix if args.prefix else '/'

    if not prefix.endswith('/') and prefix != '/':
        entry = image.find_entry(prefix)
        if entry and entry.entry_type != 'd':
            sys.stderr.write(f'error: {prefix}: not a directory\n')
            return 1
        prefix = prefix + '/'

    entries = []
    for entry in image.entries:
        if not entry.path.startswith(prefix):
            continue

        if entry.path == prefix:
            continue

        relative_path = entry.path[len(prefix):]
        if not relative_path:
            continue

        if '/' in relative_path.rstrip('/'):
            continue

        if not args.all and relative_path.startswith('.'):
            continue

        entries.append(entry)

    for entry in entries:
        basename = entry.path[len(prefix):].rstrip('/')
        if not basename:
            basename = '/'

        if args.long:
            type_char = entry.entry_type
            if args.human:
                size_str = format_size(entry.data_size)
            else:
                size_str = str(entry.data_size)

            if image.version == 2:
                mode_str = oct(entry.mode)[2:].zfill(4)
                print(f'{type_char} {mode_str} {entry.uid:4} {entry.gid:4} {size_str:>8} {basename}')
            else:
                print(f'{type_char} {size_str:>8} {basename}')
        else:
            print(basename)

    return 0


def cmd_stats(image: InitrdImage, args) -> int:
    """Print statistics about the initrd image."""
    metadata_size = image.get_metadata_size()
    data_size = image.get_data_size()

    print(f'initrd image: {args.file}')
    print(f'  version: {image.signature}')
    print(f'  entries: {image.entry_count}')
    print(f'  total size: {format_size(image.total_size)}')
    print(f'  metadata size: {format_size(metadata_size)}')
    print(f'  data size: {format_size(data_size)}')
    print(f'  data offset: 0x{image.data_offset:x}')

    file_count = sum(1 for e in image.entries if e.entry_type == 'f')
    dir_count = sum(1 for e in image.entries if e.entry_type == 'd')
    link_count = sum(1 for e in image.entries if e.entry_type == 'l')

    print(f'  files: {file_count}')
    print(f'  directories: {dir_count}')
    print(f'  symlinks: {link_count}')

    return 0


def cmd_cat(image: InitrdImage, args) -> int:
    """Output the contents of a file."""
    path = args.path

    entry = image.find_entry(path)
    if entry is None:
        sys.stderr.write(f'error: path not found: {path}\n')
        return 1

    if entry.entry_type == 'd':
        sys.stderr.write(f'error: {path}: is a directory\n')
        return 1

    if entry.entry_type == 'f':
        data = image.read_file_data(args.file, entry)
        sys.stdout.buffer.write(data)
        return 0

    sys.stderr.write(f'error: unexpected entry type: {entry.entry_type}\n')
    return 1


def cmd_add(image: InitrdImage, args) -> int:
    """Add directives to the initrd image."""
    directives = image.to_directives(args.file)
    temp_files = [d.operand for d in directives if d.isfile()]

    try:
        for directive_str in args.directives:
            try:
                new_directive = parse_directive(directive_str)
                existing = image.find_entry(new_directive.path)
                if existing:
                    sys.stderr.write(f'warning: replacing existing path: {new_directive.path}\n')
                    directives = [d for d in directives if d.path != new_directive.path]
                directives.append(new_directive)
            except argparse.ArgumentTypeError as e:
                sys.stderr.write(f'error: {e}\n')
                return 1

        image.save(args.file, directives)

        print(f'successfully updated {args.file}')
        return 0
    finally:
        for tmp_file in temp_files:
            if tmp_file and os.path.exists(tmp_file):
                os.unlink(tmp_file)


def cmd_rm(image: InitrdImage, args) -> int:
    """Remove paths from the initrd image."""
    directives = image.to_directives(args.file)
    temp_files = [d.operand for d in directives if d.isfile()]

    try:
        paths_to_remove = set()
        for path in args.paths:
            if not posixpath.isabs(path):
                sys.stderr.write(f'error: path must be absolute: {path}\n')
                return 1

            entry = image.find_entry(path)
            if not entry:
                sys.stderr.write(f'error: path not found: {path}\n')
                return 1

            if entry.entry_type == 'd' and not args.recursive:
                children = [e for e in image.entries if e.path.startswith(path) and e.path != path]
                if children:
                    sys.stderr.write(f'error: {path}: is a directory (use -r to remove recursively)\n')
                    return 1

            paths_to_remove.add(path)

            if args.recursive and entry.entry_type == 'd':
                for e in image.entries:
                    if e.path.startswith(path) and e.path != path:
                        paths_to_remove.add(e.path)

        directives = [d for d in directives if d.path not in paths_to_remove]

        if not directives:
            sys.stderr.write('error: cannot remove all entries from initrd\n')
            return 1

        image.save(args.file, directives)

        print(f'successfully updated {args.file}')
        return 0
    finally:
        for tmp_file in temp_files:
            if tmp_file and os.path.exists(tmp_file):
                os.unlink(tmp_file)


def cmd_du(image: InitrdImage, args) -> int:
    """Display disk usage of directories."""
    prefix = args.prefix if args.prefix else '/'

    if not prefix.endswith('/'):
        prefix = prefix + '/'

    max_depth = None
    if args.summary:
        max_depth = 0
    elif args.depth is not None:
        max_depth = args.depth

    dir_sizes = {}

    for entry in image.entries:
        if not entry.path.startswith(prefix):
            continue

        if entry.entry_type == 'f':
            if '/' in entry.path.rstrip('/'):
                file_dir = entry.path.rsplit('/', 1)[0] + '/'
                if file_dir == '/':
                    file_dir = '/'
            else:
                file_dir = '/'

            path = file_dir
            while True:
                if path.startswith(prefix) or path == prefix:
                    if path not in dir_sizes:
                        dir_sizes[path] = 0
                    dir_sizes[path] += entry.data_size

                if path == prefix or path == '/':
                    break

                if path == '/' or not path.rstrip('/'):
                    break
                path = path.rstrip('/').rsplit('/', 1)[0] + '/'
                if path == '/':
                    path = '/'

    if prefix not in dir_sizes:
        dir_sizes[prefix] = 0

    prefix_depth = prefix.rstrip('/').count('/')
    results = []

    for dir_path, size in dir_sizes.items():
        dir_depth = dir_path.rstrip('/').count('/')
        relative_depth = dir_depth - prefix_depth

        if max_depth is None or relative_depth <= max_depth:
            results.append((dir_path, size))

    results.sort(key=lambda x: x[0])

    for dir_path, size in results:
        if args.human:
            size_str = format_size(size)
        else:
            size_str = str(size)

        print(f'{size_str}\t{dir_path}')

    return 0


class CustomHelpFormatter(argparse.HelpFormatter):
    def _fill_text(self, text, width, indent):
        return ''.join(indent + line for line in text.splitlines(keepends=True))

    def _split_lines(self, text, width):
        return text.splitlines()

    def _format_action(self, action):
        result = super()._format_action(action)
        if action.nargs == argparse.PARSER:
            lines = result.split('\n')
            if len(lines) > 0 and not lines[0].strip():
                return '\n'.join(lines[1:])
        return result


def main():
    parser = argparse.ArgumentParser(
        description='Utility for examining initrd filesystem images.',
        formatter_class=CustomHelpFormatter
    )

    subparsers = parser.add_subparsers(dest='command', title='subcommands', metavar='')

    ls_parser = subparsers.add_parser('ls', help='list files in the image')
    ls_parser.add_argument('-f', '--file', required=True, metavar='<file>', dest='file',
                          help='path to initrd image')
    ls_parser.add_argument('-a', dest='all', action='store_true',
                          help='include entries whose names begin with a dot')
    ls_parser.add_argument('-l', dest='long', action='store_true',
                          help='print name, entry type and file size for each entry')
    ls_parser.add_argument('-H', dest='human', action='store_true',
                          help='print sizes in human readable format (requires -l)')
    ls_parser.add_argument('prefix', nargs='?', default='/',
                          help='path prefix to list (default: /)')

    stats_parser = subparsers.add_parser('stats', help='print statistics about the image')
    stats_parser.add_argument('-f', '--file', required=True, metavar='<file>', dest='file',
                             help='path to initrd image')

    cat_parser = subparsers.add_parser('cat', help='output file contents to stdout')
    cat_parser.add_argument('-f', '--file', required=True, metavar='<file>', dest='file',
                           help='path to initrd image')
    cat_parser.add_argument('path', help='path to file in the image')

    du_parser = subparsers.add_parser('du', help='display disk usage of directories')
    du_parser.add_argument('-f', '--file', required=True, metavar='<file>', dest='file',
                          help='path to initrd image')
    du_parser.add_argument('-d', '--depth', type=int, metavar='<depth>', dest='depth',
                          help='display entries for directories <depth> directories deep')
    du_parser.add_argument('-s', dest='summary', action='store_true',
                          help='display only total size for prefix (equivalent to -d 0)')
    du_parser.add_argument('-H', dest='human', action='store_true',
                          help='print sizes in human readable format')
    du_parser.add_argument('prefix', nargs='?', default='/',
                          help='path prefix to analyze (default: /)')

    add_parser = subparsers.add_parser('add', help='add directives to the image')
    add_parser.add_argument('-f', '--file', required=True, metavar='<file>', dest='file',
                           help='path to initrd image')
    add_parser.add_argument('directives', nargs='+', metavar='directive',
                           help='directives to add (format: <srcfile>:<path>, l<target>:<path>, or :<path>/)')

    rm_parser = subparsers.add_parser('rm', help='remove paths from the image')
    rm_parser.add_argument('-f', '--file', required=True, metavar='<file>', dest='file',
                          help='path to initrd image')
    rm_parser.add_argument('-r', dest='recursive', action='store_true',
                          help='recursively delete directories')
    rm_parser.add_argument('paths', nargs='+', metavar='path',
                          help='paths to remove from the image')

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return 1

    try:
        image = InitrdImage.load(args.file)
    except FileNotFoundError:
        sys.stderr.write(f'error: file not found: {args.file}\n')
        return 1
    except Exception as e:
        sys.stderr.write(f'error: {e}\n')
        return 1

    if args.command == 'ls':
        return cmd_ls(image, args)
    elif args.command == 'stats':
        return cmd_stats(image, args)
    elif args.command == 'cat':
        return cmd_cat(image, args)
    elif args.command == 'du':
        return cmd_du(image, args)
    elif args.command == 'add':
        return cmd_add(image, args)
    elif args.command == 'rm':
        return cmd_rm(image, args)
    else:
        sys.stderr.write(f'error: unknown command: {args.command}\n')
        return 1


if __name__ == '__main__':
    sys.exit(main())
