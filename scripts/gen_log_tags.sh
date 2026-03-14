#!/bin/sh
# Generate per-tag log headers in build/log_tags/
# Usage: gen_log_tags.sh <build_dir> <enabled_tags...>
set -e

BUILD_DIR="$1"
shift
ENABLED_TAGS=" $* "

TAG_DIR="$BUILD_DIR/log_tags"
mkdir -p "$TAG_DIR"

# find all LOG_TAG definitions in source
ALL_TAGS=$(grep -rh '#define LOG_TAG' kernel/ drivers/ fs/ 2>/dev/null | sed 's/.*LOG_TAG //' | sort -u)

for tag in $ALL_TAGS; do
    file="$TAG_DIR/$tag.h"
    if echo "$ENABLED_TAGS" | grep -q " $tag " || echo "$ENABLED_TAGS" | grep -q " __all__ "; then
        content="#define LOG_ENABLED_${tag} 1"
    else
        content=""
    fi
    tmpfile="$file.tmp"
    printf '%s\n' "$content" > "$tmpfile"
    if cmp -s "$tmpfile" "$file" 2>/dev/null; then
        rm "$tmpfile"
    else
        mv "$tmpfile" "$file"
    fi
done
