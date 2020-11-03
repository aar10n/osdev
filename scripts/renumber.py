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
with open(fname, 'r') as file:
  with open(f'{ftext}_new{fext}', 'w') as new_file:
    last_num = None
    for line in file:
      s = re.sub(r'^\s*|\s\s+', '', line.split('//')[0])
      if len(s) == 0:
        new_file.write(line)
        continue

      match = re.match('^#define ([a-zA-Z_][a-zA-Z0-9_]*) ([0-9]+)', s)
      if not match:
        new_file.write(line)
        continue

      num = int(match.group(2))
      print(num, last_num)
      if num == last_num:
        new_line = line.replace(str(num), str(num + 1), 1)
        print(new_line)
        new_file.write(new_line)
        last_num = num + 1
      else:
        last_num = num
        new_file.write(line)
