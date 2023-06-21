#!/usr/bin/env python
import sys
import re
import os

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))

STACK_FRAME_PATTERN = re.compile(r"\?\? (?P<addr>0x[0-9a-f]+)")
ADDR2LINE = os.environ.get('ADDR2LINE', os.path.join(PROJECT_DIR, 'build/sysroot/usr/bin/x86_64-osdev-addr2line'))


def main():
    if len(sys.argv) != 3:
        print("Usage: python3 debug_backtrace.py <log_file> <kernel elf>")
        return

    frames = []
    log_file = sys.argv[1]
    kernel_elf = sys.argv[2]
    with open(log_file, 'r') as f:
        in_stacktrace = False
        for line in f:
            line = line.strip()
            if line == "backtrace":
                in_stacktrace = True
                continue

            if in_stacktrace and line != "":
                match = STACK_FRAME_PATTERN.match(line)
                if match:
                    frames.append(match.group('addr'))
                else:
                    in_stacktrace = False

    print('backtrace:')
    for frame in frames:
        cmd = f"{ADDR2LINE} -e {kernel_elf} -fpi {frame}"
        print(f"  {frame} -> {os.popen(cmd).read().strip()}")


if __name__ == '__main__':
    main()
