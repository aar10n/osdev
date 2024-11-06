python

# Custom Printers to format cstr_t and str_t objects like real strings.

class StrPrinter(gdb.ValuePrinter):
    """Print a str or cstr object."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        if self.val is None:
          return '(null)'
        if self.val['str'] == 0 or self.val['len'] == 0:
          return '(empty)'
        return f'"{self.val['str'].string(length=self.val['len'])}"'


def build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter('custom_str_printers')
    pp.add_printer('cstr', '^cstr$', StrPrinter)
    pp.add_printer('str', '^str$', StrPrinter)
    return pp


gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pretty_printer())

# https://stackoverflow.com/questions/33049201/gdb-add-symbol-file-all-sections-and-load-address
#
#   add-symbol-file-all <file> <address>
#
# Loads the symbols from the given file at the specified base address.
# Useful for debugging dynamically linked libraries.
import subprocess
import re

def relocatesections(filename, addr):
    p = subprocess.Popen(["build/toolchain/bin/x86_64-linux-musl-readelf", "-SW", filename], stdout = subprocess.PIPE)

    sections = []
    textaddr = '0'
    for line in p.stdout.readlines():
        line = line.decode("utf-8").strip()
        if not line.startswith('[') or line.startswith('[Nr]'):
            continue

        line = re.sub(r' +', ' ', line)
        line = re.sub(r'\[ *(\d+)\]', r'\g<1>', line)
        fieldsvalue = line.split(' ')
        fieldsname = ['number', 'name', 'type', 'addr', 'offset', 'size', 'entsize', 'flags', 'link', 'info', 'addralign']
        sec = dict(zip(fieldsname, fieldsvalue))

        if sec['number'] == '0':
            continue

        sections.append(sec)

        if sec['name'] == '.text':
            textaddr = sec['addr']

    return (textaddr, sections)


class AddSymbolFileAll(gdb.Command):
    """The right version for add-symbol-file"""

    def __init__(self):
        super(AddSymbolFileAll, self).__init__("add-symbol-file-all", gdb.COMMAND_USER)
        self.dont_repeat()

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        filename = argv[0]

        if len(argv) > 1:
            offset = int(str(gdb.parse_and_eval(argv[1])), 0)
        else:
            offset = 0

        (textaddr, sections) = relocatesections(filename, offset)

        cmd = "add-symbol-file %s 0x%08x" % (filename, int(textaddr, 16) + offset)

        for s in sections:
            addr = int(s['addr'], 16)
            if s['name'] == '.text' or addr == 0:
                continue

            cmd += " -s %s 0x%08x" % (s['name'], addr + offset)

        gdb.execute(cmd)

class RemoveSymbolFileAll(gdb.Command):
    """The right version for remove-symbol-file"""

    def __init__(self):
        super(RemoveSymbolFileAll, self).__init__("remove-symbol-file-all", gdb.COMMAND_USER)
        self.dont_repeat()

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        filename = argv[0]

        if len(argv) > 1:
            offset = int(str(gdb.parse_and_eval(argv[1])), 0)
        else:
            offset = 0

        (textaddr, _) = relocatesections(filename, offset)

        cmd = "remove-symbol-file -a 0x%08x" % (int(textaddr, 16) + offset)
        gdb.execute(cmd)


AddSymbolFileAll()
RemoveSymbolFileAll()
end

set disassembly-flavor intel
add-symbol-file build/kernel.elf

# Loading in-kernel debugging info is too slow to be used while also
# debugging with gdb. We disable it via the runtime option before the
# kernel starts.
b kmain
commands
  set variable is_debug_enabled = 0
  add-symbol-file-all build/musl/x86_64-linux-musl/lib/libc.so 0x7fc0000000
  add-symbol-file-all build/sbin/init/init 0x00000000000
  #b _dlstart_c
  #b _start_c
end
