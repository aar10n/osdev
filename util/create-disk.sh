#!/bin/bash

OUTFILE=$1
dd if=/dev/zero of=$OUTFILE bs=1024 count=64
mkfs.ext2 -t ext2 -L root $OUTFILE

echo "Hello, world!" > hello.txt
echo "Another file..." > another.txt

e2cp hello.txt $OUTFILE:/hello.txt
e2mkdir $OUTFILE:/home
e2cp another.txt $OUTFILE:/home/another.txt

rm hello.txt
rm another.txt
