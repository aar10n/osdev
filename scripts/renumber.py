#!/bin/python3
import sys
import re
import os

if len(sys.argv) < 2:
  sys.stderr.write('renumber.py <file>\n')
  sys.exit(1)

fname = sys.argv[1]
ftext = os.path.splitext(fname)[0]
fext = os.path.splitext(fname)[1]

start_num = int(sys.argv[2]) if len(sys.argv) > 2 else 0
num = start_num
with open(fname, 'r') as file:
  with open(f'{ftext}_new{fext}', 'w') as new_file:
    for line in file:
      s = re.sub(r'^\s*|\s\s+', '', line.split('//')[0])
      if len(s) == 0:
        new_file.write(line)
        continue

      match = re.match('^#define ([a-zA-Z_][a-zA-Z0-9_]*) ([0-9]+)', s)
      if not match:
        new_file.write(line)
        continue

      new_line = line.replace(match.group(2), str(num), 1)
      new_file.write(new_line)
      num += 1

