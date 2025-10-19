#!/usr/bin/env python
"""
Generate an initrd filesystem image.

Supports both v1 and v2 initrd formats.
"""

import argparse
import sys
from initrd import InitrdImage, Directive
from initrd.directive import parse_directive


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


usage_text = """\
%(prog)s -o <file> [-f <file>] [options] <directive>...
"""

epilog_text = """
directives:
    :<path>/          create an empty directory at <path>
    l<target>:<path>  create a symlink to <target> at <path>
    <srcfile>:<path>  add <srcfile> to the image at <path>

examples:
    # Create a v2 image
    %(prog)s -o initrd.img -F v2 -f directives.txt

    # Create a v1 image
    %(prog)s -o initrd.img -F v1 -f directives.txt
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
    parser.add_argument('-F', '--format', choices=['v1', 'v2'], default='v2', dest='format',
                        help="initrd format version (default: v2)")
    parser.add_argument('directives', nargs='*', help=argparse.SUPPRESS)

    args = parser.parse_args()

    # convert format to version number
    version = 1 if args.format == 'v1' else 2

    # parse directives
    directives = []
    for filename in args.infiles:
        try:
            with open(filename, 'r') as f:
                for line in f.read().splitlines():
                    line = line.strip()
                    if line and not line.startswith('#'):
                        d = parse_directive(line)
                        directives.append(d)
        except FileNotFoundError:
            parser.error(f'file not found: {filename}')
        except Exception as e:
            parser.error(f'{e}')

    # add command-line directives
    for directive_str in args.directives:
        try:
            d = parse_directive(directive_str)
            directives.append(d)
        except Exception as e:
            parser.error(f'{e}')

    if not directives:
        parser.error('no directives specified')

    # create and save the image
    try:
        img = InitrdImage(version=version)
        img.save(args.outfile, directives)

        print(f'wrote {img.total_size} bytes to {args.outfile}')
        print(f'  version: {version}')
        print(f'  entry count: {img.entry_count}')
        print(f'  data offset: {img.data_offset}')
        print(f'  data size: {img.get_data_size()}')

    except Exception as e:
        sys.stderr.write(f'error: {e}\n')
        sys.exit(1)
