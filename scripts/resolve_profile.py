#!/usr/bin/env python3
"""

Symbol resolver for QEMU profiles

This script takes folded stack traces with raw addresses and resolves them
to symbol names using a symbol file (produced with `gen_syms.py`).

Usage:
    ./resolve_symbols.py profile.folded kernel.syms > profile-resolved.folded

Symbol file format:
    ffffffff80000000 _start
    ffffffff80000100 kmain
    ...
"""
import sys
import re
import argparse


def load_symbols(symfile):
    """Load symbols from a symbol file"""
    symbols = []
    with open(symfile) as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 2:
                try:
                    addr = int(parts[0], 16)
                    name = parts[1]
                    symbols.append((addr, name))
                except ValueError:
                    continue
    symbols.sort()
    return symbols


def is_garbage_address(addr):
    """Check if address looks like garbage"""
    # common garbage patterns
    garbage_patterns = [
        0xafafafafafafafaf, 0xdeadbeefdeadbeef,
        0x5555555555555555, 0xaaaaaaaaaaaaaaaa,
        0x0123456789abcdef, 0xfedcba9876543210
    ]
    
    if addr in garbage_patterns:
        return True
    
    # check for unlikely addresses
    if addr == 0 or addr == 0xffffffffffffffff:
        return True
        
    # check alignment (most code is at least 2-byte aligned)
    if addr & 0x1:
        return True
        
    return False


def resolve_address(addr, symbols):
    """Binary search to find symbol for address"""
    if not symbols:
        return f"0x{addr:x}"

    lo, hi = 0, len(symbols) - 1
    best = None

    while lo <= hi:
        mid = (lo + hi) // 2
        if symbols[mid][0] <= addr:
            best = mid
            lo = mid + 1
        else:
            hi = mid - 1

    if best is not None:
        sym_addr, sym_name = symbols[best]
        offset = addr - sym_addr
        # sanity check - functions shouldn't be huge
        if offset < 0x10000:
            if offset == 0:
                return sym_name
            else:
                return f"{sym_name}+0x{offset:x}"

    return f"0x{addr:x}"


def parse_frame(frame):
    """
    Parse different frame formats:
    - 0x1234abcd - raw address
    - kernel`func+0x123 - module`symbol+offset
    - user`func+0x123 - module`symbol+offset
    - [interrupt] - interrupt boundary marker
    - [syscall_return] - syscall return boundary marker
    """

    # raw hex address
    if frame.startswith('0x'):
        try:
            return int(frame, 16), None, None
        except ValueError:
            return None, None, frame

    # module`symbol+offset format
    match = re.match(r'(\w+)`(\w+)\+0x([0-9a-fA-F]+)', frame)
    if match:
        module = match.group(1)
        symbol = match.group(2)
        offset = int(match.group(3), 16)
        # for generic symbols like "func", we want to resolve
        if symbol in ['func', 'function']:
            return offset, module, None
        # otherwise keep the existing symbol
        return None, module, f"{symbol}+0x{offset:x}"

    # module`address format (from plugin output)
    match = re.match(r'(\w+)`0x([0-9a-fA-F]+)', frame)
    if match:
        module = match.group(1)
        addr = int(match.group(2), 16)
        # filter out garbage addresses
        if is_garbage_address(addr):
            return None, None, f"{module}`0x{addr:x}[INVALID]"
        return addr, module, None

    # unknown format, keep as-is
    return None, None, frame


def resolve_folded_stacks(input_file, symbols):
    """Process folded stack file and resolve addresses"""
    with open(input_file) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            parts = line.split(' ')
            if len(parts) != 2:
                print(f"Warning: Invalid line format: {line}", file=sys.stderr)
                continue

            stack, count = parts
            frames = stack.split(';')
            resolved = []

            for frame in frames:
                addr, module, resolved_name = parse_frame(frame)

                if resolved_name:
                    # already resolved
                    resolved.append(f"{module}`{resolved_name}" if module else resolved_name)
                elif addr is not None:
                    # need to resolve
                    resolved_sym = resolve_address(addr, symbols)
                    if module:
                        resolved.append(f"{module}`{resolved_sym}")
                    else:
                        resolved.append(resolved_sym)
                else:
                    # keep as-is
                    resolved.append(frame)

            print(f"{';'.join(resolved)} {count}")


def main():
    parser = argparse.ArgumentParser(
        description='Resolve addresses in folded stack traces to symbols'
    )
    parser.add_argument('profile', help='Folded stack trace profile from QEMU plugin')
    parser.add_argument('symbols', nargs='?', help='Symbol file')

    args = parser.parse_args()

    symbols = []
    if args.symbols:
        symbols = load_symbols(args.symbols)

    if not symbols:
        print("No symbols loaded. Please provide a valid symbol file.", file=sys.stderr)
        sys.exit(1)

    resolve_folded_stacks(args.folded, symbols)


if __name__ == "__main__":
    main()
