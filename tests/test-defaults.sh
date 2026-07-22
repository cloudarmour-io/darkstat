#!/bin/sh
set -eu

require_line() {
  file=$1
  line=$2
  if ! grep -Fxq -- "$line" "$file"; then
    echo "missing expected line in $file: $line" >&2
    exit 1
  fi
}

require_line debian/darkstat-nw.default "HOSTS_MAX=1000"
require_line debian/darkstat-nw.default "HOSTS_KEEP=500"
require_line debian/darkstat-nw.default "PORTS_MAX=60"
require_line debian/darkstat-nw.default "PORTS_KEEP=30"
require_line debian/darkstat-nw.default "HIGHEST_PORT=65535"
require_line debian/darkstat-nw.default "HOST_RETENTION_HOURS=1"
require_line debian/darkstat-nw.default "MEM_LIMIT_MB=256"
require_line debian/darkstat-nw.default "API_KEY="
