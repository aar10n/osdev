#!/bin/bash

PATCH_TARGET=$1
PATCH_FILE=$2

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


# validate the patch file
if [[ -z "$PATCH_FILE" ]]; then
  >&2 echo "no patch given"
  exit 1
elif [[ ! -f "$PATCH_FILE" ]]; then
  if [[ -e "$PATCH_FILE" ]]; then
    >&2 echo "patch is not a file"
  else
    >&2 echo "patch does not exist"
  fi
  exit 1
fi

# undo all patches (dont use revert just in case patch has changed)
for orig_file in $(find $PATCH_TARGET -type f -name "*.orig"); do
  file=$(echo $orig_file | sed 's/\.orig$//')
  mv $orig_file $file
done
# delete all rejects
find $PATCH_TARGET -type f -name "*.rej" -delete

if (patch -p1 -N --dry-run --silent -d $PATCH_TARGET < $PATCH_FILE) > /dev/null; then
  patch -p1 -N -b -r /dev/null -d $PATCH_TARGET < $PATCH_FILE
  echo "patch successful"
  exit 0
else
  >&2 echo "patch failed"
  exit 1
fi
