#!/usr/bin/env python3
"""
Generate combined symbol table from kernel and program ELF files

This script extracts symbols from ELF files and creates a unified symbol
table suitable for use with the QEMU profile resolver.

Usage:
    ./generate-syms.py -k kernel.elf -p prog1 -p lib.so@0x7ff000000000 -o system.syms
"""

import argparse
import subprocess
import sys
from pathlib import Path


class SymbolExtractor:
    """Extract symbols from ELF files using nm"""

    def __init__(self, annotate_source=False):
        self.symbols = []
        self.annotate_source = annotate_source

    def extract_symbols(self, elf_path, base_address=0, source_name=None):
        """Extract symbols from an ELF file with optional base address adjustment"""

        if not Path(elf_path).exists():
            print(f"Error: File not found: {elf_path}", file=sys.stderr)
            return False

        # determine source name for annotation
        if source_name is None:
            source_name = Path(elf_path).name

        try:
            # run nm with numeric sort and all symbols
            result = subprocess.run(
                ['nm', '-n', '--defined-only', elf_path],
                capture_output=True,
                text=True,
                check=False
            )

            if result.returncode != 0:
                print(f"Warning: nm failed for {elf_path}: {result.stderr}", file=sys.stderr)
                # try without --defined-only flag
                result = subprocess.run(
                    ['nm', '-n', elf_path],
                    capture_output=True,
                    text=True,
                    check=False
                )

                if result.returncode != 0:
                    print(f"Error: Cannot read symbols from {elf_path}", file=sys.stderr)
                    return False

            # parse nm output
            for line in result.stdout.splitlines():
                parts = line.strip().split()
                if len(parts) >= 3:
                    # format: address type symbol_name
                    addr_str = parts[0]
                    sym_type = parts[1]
                    sym_name = parts[2]

                    # skip certain symbol types
                    if sym_type in ['U', 'w']:  # undefined or weak undefined
                        continue

                    # skip certain symbol names
                    if sym_name.startswith('.') or sym_name == '':
                        continue

                    try:
                        addr = int(addr_str, 16)
                        # apply base address adjustment
                        adjusted_addr = addr + base_address

                        # optionally annotate with source
                        if self.annotate_source:
                            annotated_name = f"{sym_name}[{source_name}]"
                        else:
                            annotated_name = sym_name

                        self.symbols.append((adjusted_addr, annotated_name))
                    except ValueError:
                        continue

            return True

        except FileNotFoundError:
            print("Error: 'nm' command not found. Please install binutils.", file=sys.stderr)
            return False
        except Exception as e:
            print(f"Error processing {elf_path}: {e}", file=sys.stderr)
            return False

    def get_sorted_symbols(self):
        """Return symbols sorted by address"""
        return sorted(self.symbols, key=lambda x: x[0])


def parse_program_spec(spec):
    """
    Parse program specification in format:
    - /path/to/program
    - /path/to/program@1234
    - /path/to/program@0x7fc0000000
    """
    if '@' in spec:
        parts = spec.rsplit('@', 1)
        if len(parts) != 2:
            raise ValueError(f"Invalid program specification: {spec}")

        path = parts[0]
        base_str = parts[1]

        # parse base address (decimal or hex)
        try:
            if base_str.startswith('0x') or base_str.startswith('0X'):
                base = int(base_str, 16)
            else:
                base = int(base_str, 10)
        except ValueError:
            raise ValueError(f"Invalid base address: {base_str}")

        return path, base
    else:
        # no base address specified, assume absolute addressing
        return spec, 0


def main():
    parser = argparse.ArgumentParser(
        description='Generate combined symbol table from kernel and program ELF files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Kernel only
  %(prog)s -k kernel.elf
  
  # Kernel with userspace programs
  %(prog)s -k kernel.elf -p sbin/init -p sbin/shell
  
  # Kernel with relocated libraries
  %(prog)s -k kernel.elf -p ld-musl-x86_64.so.1@0x7fc0000000
  
  # With source annotation
  %(prog)s -k kernel.elf -p libc.so@0x7fc0000000 --annotate-source
  
  # Custom output file
  %(prog)s -k kernel.elf -p prog1 -p prog2 -o system.syms
"""
    )
    parser.add_argument('-k', '--kernel', required=True,
                        help='Path to kernel ELF file')
    parser.add_argument('-p', '--program', action='append', default=[],
                        help='Additional program to include (can be specified multiple times)')
    parser.add_argument('-o', '--out-file', default='out.syms',
                        help='Output symbol file (default: out.syms)')
    parser.add_argument('-a', '--annotate-source', action='store_true',
                        help='Annotate symbols with source file name (e.g., symbol[libc.so])')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose output')

    args = parser.parse_args()

    extractor = SymbolExtractor(annotate_source=args.annotate_source)

    # extract kernel symbols first
    if args.verbose:
        print(f"Extracting symbols from kernel: {args.kernel}", file=sys.stderr)

    if not extractor.extract_symbols(args.kernel):
        print("Error: Failed to extract kernel symbols", file=sys.stderr)
        return 1

    kernel_sym_count = len(extractor.symbols)
    if args.verbose:
        print(f"  Found {kernel_sym_count} kernel symbols", file=sys.stderr)

    # extract program symbols
    for prog_spec in args.program:
        try:
            prog_path, base_addr = parse_program_spec(prog_spec)

            if args.verbose:
                if base_addr != 0:
                    print(f"Extracting symbols from program: {prog_path} @ 0x{base_addr:x}",
                          file=sys.stderr)
                else:
                    print(f"Extracting symbols from program: {prog_path}", file=sys.stderr)

            prev_count = len(extractor.symbols)
            if not extractor.extract_symbols(prog_path, base_addr):
                print(f"Warning: Failed to extract symbols from {prog_path}", file=sys.stderr)
                continue

            new_syms = len(extractor.symbols) - prev_count
            if args.verbose:
                print(f"  Found {new_syms} symbols", file=sys.stderr)

        except ValueError as e:
            print(f"Error: {e}", file=sys.stderr)
            return 1

    # get sorted symbols
    all_symbols = extractor.get_sorted_symbols()

    if not all_symbols:
        print("Error: No symbols found", file=sys.stderr)
        return 1

    # write to the output file
    try:
        with open(args.out_file, 'w') as f:
            for addr, name in all_symbols:
                f.write(f"{addr:016x} {name}\n")

        if args.verbose:
            print(f"\nWrote {len(all_symbols)} total symbols to {args.out_file}",
                  file=sys.stderr)
            print(f"  Kernel symbols: {kernel_sym_count}", file=sys.stderr)
            print(f"  Program symbols: {len(all_symbols) - kernel_sym_count}", file=sys.stderr)

    except IOError as e:
        print(f"Error writing output file: {e}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
