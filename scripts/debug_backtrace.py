#!/usr/bin/env python3
import os
import argparse
import re
import sys
import subprocess

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
ADDR2LINE = os.environ.get('ADDR2LINE', os.path.join(PROJECT_DIR, 'build/toolchain/bin/x86_64-linux-musl-addr2line'))
READELF = os.environ.get('READELF', os.path.join(PROJECT_DIR, 'build/toolchain/bin/x86_64-linux-musl-readelf'))

STACK_FRAME_PATTERN = re.compile(r'^\s*\?\?\s+(?P<addr>0x[0-9a-f]+)')
DESCRIPTOR_PATTERN = re.compile(r'^[^@]+(@(0x[0-9a-fA-F]+|\d+))?$')
KERNEL_BASE_ADDR = 0xffff800000000000


class Program:
    def __init__(self, path, base_addr, size):
        self.path = path
        self.base_addr = base_addr
        self.size = size

    def contains(self, addr):
        return self.base_addr <= addr < self.base_addr + self.size


def parse_descriptor(descriptor, sysroot=None):
    if not DESCRIPTOR_PATTERN.match(descriptor):
        raise ValueError(f"Invalid descriptor format: {descriptor}")

    parts = descriptor.split('@')
    path = parts[0]

    if sysroot and os.path.abspath(path):
        path = os.path.join(sysroot, path.lstrip('/'))

    if len(parts) == 2:
        base_addr = int(parts[1], 16) if parts[1].startswith('0x') else int(parts[1])
    else:
        base_addr = None

    return path, base_addr


def get_file_size(path):
    return os.stat(path).st_size


def has_debug_symbols(path):
    try:
        # use readelf to check for debug sections
        result = subprocess.run(
            [READELF, '-S', path],
            capture_output=True,
            text=True,
            check=True
        )
        # check for common debug section names
        return any(section in result.stdout for section in ['.debug_info', '.debug_line', '.debug_str'])
    except subprocess.CalledProcessError:
        return False


def decode_address(program, addr):
    offset = addr - program.base_addr
    try:
        result = subprocess.run(
            [ADDR2LINE, '-e', program.path, hex(offset)],
            capture_output=True,
            text=True,
            check=True
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError:
        return None


def main():
    parser = argparse.ArgumentParser(description='Debug backtrace from kernel log')
    parser.add_argument('logfile', help='Path to kernel log file')
    parser.add_argument('-k', '--kernel', required=True, help='Path to kernel elf file')
    parser.add_argument('-p', '--program', action='append', default=[], help='Path to program elf file(s)')
    parser.add_argument('-f', '--file', help='Path to memory map descriptor file')
    parser.add_argument('-S', '--sysroot', help='Path to use as the system root')

    args = parser.parse_args()
    programs = []

    # validate and add kernel
    if not has_debug_symbols(args.kernel):
        print(f"Warning: {args.kernel} appears to lack debug symbols", file=sys.stderr)
    kernel_size = get_file_size(args.kernel)
    programs.append(Program(args.kernel, KERNEL_BASE_ADDR, kernel_size))

    descriptors = args.program[:]

    if args.file:
        with open(args.file, 'r') as f:
            for line in f:
                line = line.strip()
                if line:
                    descriptors.append(line)

    # parse descriptors
    for descriptor in descriptors:
        path = None
        base_addr = None
        try:
            path, base_addr = parse_descriptor(descriptor, args.sysroot)
        except ValueError as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

        if base_addr is None:
            continue

        if not os.path.exists(path):
            print(f"Error: File not found: {path}", file=sys.stderr)
            sys.exit(1)

        if not has_debug_symbols(path):
            print(f"Warning: {path} appears to lack debug symbols", file=sys.stderr)

        size = get_file_size(path)
        programs.append(Program(path, base_addr, size))

    # process log file
    with open(args.logfile, 'r') as f:
        lines = f.readlines()

    i = 0
    while i < len(lines):
        if lines[i].strip() == 'backtrace':
            print('backtrace')
            i += 1

            # process addresses
            while i < len(lines):
                match = STACK_FRAME_PATTERN.match(lines[i])
                if not match:
                    break

                addr = int(match.group('addr'), 16)

                # find containing program
                decoded = None
                for program in programs:
                    if program.contains(addr):
                        decoded = decode_address(program, addr)
                        break

                if decoded:
                    print(f'  {decoded}')
                else:
                    print(f'  ?? {hex(addr)}')

                i += 1
        else:
            i += 1


if __name__ == '__main__':
    main()
