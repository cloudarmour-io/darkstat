#!/bin/sh
set -eu

if [ ! -x ./darkstat ]; then
  echo "darkstat binary not found; run make first" >&2
  exit 1
fi

help=$(./darkstat --help)

printf '%s\n' "$help" | grep -Fq -- '--mem-limit-mb'
printf '%s\n' "$help" | grep -Fq -- '--api-key-md5'
printf '%s\n' "$help" | grep -Fq -- '--host-retention-hours'
printf '%s\n' "$help" | grep -Fq -- '--hosts-max'
printf '%s\n' "$help" | grep -Fq -- '--ports-keep'
