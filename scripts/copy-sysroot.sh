#!/bin/bash

SYSROOT=$1
FILES="${@:2}"

for file in $FILES; do
  SOURCE="${file%:*}"
  DEST="$SYSROOT${file#*:}"
  cp $SOURCE $DEST
done

