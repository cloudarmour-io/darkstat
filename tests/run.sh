#!/bin/sh
set -eu

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/.." && pwd)

cd "$ROOT"

"$DIR/test-defaults.sh"
"$DIR/test-traffic.sh"
"$DIR/test-host-trim.sh"
"$DIR/test-wrapper.sh"
"$DIR/test-help.sh"
