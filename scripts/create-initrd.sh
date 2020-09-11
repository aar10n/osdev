#!/bin/bash

EXE=$1
OUTFILE=$2

echo "Hello, world!" > hello.txt
echo "Another file..." > another.txt

$EXE -v -b 512 -o $2 create hello.txt:/hello.txt another.txt:/home/another.txt

rm hello.txt
rm another.txt

