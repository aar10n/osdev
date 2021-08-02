#!/bin/bash

PATCH_TARGET=$1

# validate the patch target
if [[ -z "$PATCH_TARGET" ]]; then
  >&2 echo "no target given"
  exit 1
elif [[ ! -d "$PATCH_TARGET" ]]; then
  if [[ -e "$PATCH_TARGET" ]]; then
    >&2 echo "target is not a directory"
  else
    >&2 echo "target does not exist"
  fi
  exit 1
fi

# undo all patches by reverting to the .orig files
for orig_file in $(find $PATCH_TARGET -type f -name "*.orig"); do
  file=$(echo $orig_file | sed 's/\.orig$//')
  mv $orig_file $file
done
# delete all leftover rejects
find $PATCH_TARGET -type f -name "*.rej" -delete
