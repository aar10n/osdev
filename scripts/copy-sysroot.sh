#!/bin/bash

SYSROOT=$1
BUILDROOT=$2
FILES="${@:3}"

for file in $FILES; do
  SOURCE="${file%:*}"
  DEST="${file#*:}"
  cp $SOURCE $SYSROOT$DEST
  if test -f "$BUILDROOT/ext2.img"; then
    e2cp $SOURCE $BUILDROOT/ext2.img:$DEST
  fi
done

