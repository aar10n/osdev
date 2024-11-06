import argparse
import re
import sys
from collections import defaultdict
from enum import Enum
from typing import Optional

alloc_pattern = re.compile(r"(?P<op>vn_alloc_empty|ve_alloc_linked|vfs_alloc): allocated (?P<name>\([^)]+\))<(?P<ptr>[^>]+)>.*")
getref_pattern = re.compile(r"(?P<op>ve_getref|vn_getref|vfs_getref) (?P<name>.*)<(?P<ptr>.*)> refcount=(?P<ref>\d+)")
release_pattern = re.compile(r"(?P<op>ve_release|ve_release_swap|vn_release|vfs_release) (?P<name>.*)<(?P<ptr>.*)> refcount=(?P<ref>\d+)")
cleanup_pattern = re.compile(r"(?P<op>ve_cleanup|vn_cleanup|vfs_cleanup) !!! (?P<name>.*)<(?P<ptr>.*)")

ve_refcounts = defaultdict(lambda: {"count": 0, "names": set(), "log_entries": []})
vn_refcounts = defaultdict(lambda: {"count": 0, "names": set(), "log_entries": []})
vfs_refcounts = defaultdict(lambda: {"count": 0, "names": set(), "log_entries": []})


class Color:
    """ ANSI color codes
        https://gist.github.com/rene-d/9e584a7dd2935d0f461904b9f2950007#file-colors-py """
    BLACK = "\033[0;30m"
    RED = "\033[0;31m"
    GREEN = "\033[0;32m"
    BROWN = "\033[0;33m"
    BLUE = "\033[0;34m"
    PURPLE = "\033[0;35m"
    CYAN = "\033[0;36m"
    BOLD = "\033[1m"
    RESET = "\033[0m"

    # zero the escape codes if stdout is not a tty
    if not __import__("sys").stdout.isatty():
        for _ in dir():
            if isinstance(_, str) and _[0] != "_":
                locals()[_] = ""

    @staticmethod
    def text(color, text):
        return f"{color}{text}{Color.RESET}"


class RefOp(Enum):
    ALLOC = 1
    GETREF = 2
    PUTREF = 3
    RELEASE = 4


def get_dict_for_op(op):
    if op.startswith("ve"):
        return ve_refcounts
    elif op.startswith("vn"):
        return vn_refcounts
    elif op.startswith("vfs"):
        return vfs_refcounts
    else:
        raise ValueError(f"Invalid operation: {op}")


def process_alloc_line(match: re.Match[str], line: str):
    op = match.group('op')
    name = match.group('name')
    ptr = match.group('ptr')

    d = get_dict_for_op(op)
    d[ptr]["count"] = 1
    d[ptr]["names"].add(name)
    d[ptr]["log_entries"].append(line)


def process_getref_line(match: re.Match[str], line: str):
    op = match.group('op')
    name = match.group('name')
    ptr = match.group('ptr')
    ref = int(match.group('ref'))

    d = get_dict_for_op(op)
    d[ptr]["count"] = ref
    d[ptr]["names"].add(name)
    d[ptr]["log_entries"].append(line)


def process_release_line(match: re.Match[str], line: str):
    op = match.group('op')
    name = match.group('name')
    ptr = match.group('ptr')
    ref = int(match.group('ref'))

    d = get_dict_for_op(op)
    d[ptr]["count"] = ref
    d[ptr]["names"].add(name)
    d[ptr]["log_entries"].append(line)


def process_log_line(line: str) -> Optional[RefOp]:
    if match := alloc_pattern.search(line):
        process_alloc_line(match, line)
        return RefOp.ALLOC
    elif match := getref_pattern.search(line):
        process_getref_line(match, line)
        return RefOp.GETREF
    elif match := release_pattern.search(line):
        process_release_line(match, line)
        return RefOp.PUTREF
    elif _ := cleanup_pattern.search(line):
        return RefOp.RELEASE
    else:
        return None


def process_log(lines: list[str], quiet: bool = False):
    print(f"Processing {len(lines)} lines (quiet={quiet})")
    for (i, line) in enumerate(lines):
        line = line.rstrip()
        if op := process_log_line(line):
            if op == RefOp.ALLOC:
                print(f"{Color.text(Color.CYAN, line)}")
            elif op == RefOp.GETREF:
                print(f"{Color.text(Color.GREEN, line)}")
            elif op == RefOp.PUTREF:
                print(f"{Color.text(Color.RED, line)}")
            elif op == RefOp.RELEASE:
                print(f"{Color.text(Color.PURPLE, line)}")
        elif line.startswith(">>>"):
            print(f"{Color.text(Color.BOLD, line)}")
        elif not quiet:
            print(line)


def print_hanging_refs(d: dict):
    for ptr, refcount in d.items():
        count = refcount["count"]
        if count == 0:
            continue

        names = ", ".join([name for name in refcount["names"] if 'null' not in name])
        print(f"{Color.text(Color.BOLD, ptr)} {count} ref(s): {names}")
        for entry in refcount["log_entries"]:
            print(f"  {entry}")


def display_hanging_refs():
    print(Color.text(Color.CYAN, "Remaining ventry refs:"))
    print_hanging_refs(ve_refcounts)
    print(Color.text(Color.BOLD, "=" * 80))
    print(Color.text(Color.CYAN, "Remaining vnode refs:"))
    print_hanging_refs(vn_refcounts)
    print(Color.text(Color.BOLD, "=" * 80))
    print(Color.text(Color.CYAN, "Remaining vfs refs:"))
    print_hanging_refs(vfs_refcounts)
    print(Color.text(Color.BOLD, "=" * 80))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Process some flags.")
    parser.add_argument('-q', '--quiet', action='store_true', help="Suppress output")
    parser.add_argument('log_file', help="The log file to process")
    args = parser.parse_args()

    if args.log_file == "-":
        log_lines = sys.stdin.readlines()
    else:
        with open(args.log_file, 'r') as log_file:
            log_lines = log_file.readlines()

    process_log(log_lines, args.quiet)
    print("=" * 80)
    display_hanging_refs()
