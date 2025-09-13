#!/bin/bash
# Wrapper script for clang-tidy with custom plugin
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PLUGIN_PATH="$SCRIPT_DIR/../../build/tools/clang-tidy-plugin/OsdevPlugin.so"
CLANG_TIDY="$(which clang-tidy)"

exec "$CLANG_TIDY" -load="$PLUGIN_PATH" "$@"
