#!/bin/bash

PROJECT_ROOT="$(cd "$(dirname "$0")"; cd ../; pwd -P)"
OUT_FILE="$PROJECT_ROOT/compile_commands.json"

(cd "$PROJECT_ROOT"; compiledb -o "$OUT_FILE" -n make all)
(cd "$PROJECT_ROOT/tools"; compiledb -o "$OUT_FILE" -n make all)
