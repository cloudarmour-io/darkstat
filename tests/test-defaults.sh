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
require_line debian/darkstat-nw.default "#API_KEY="

# Default bind must be loopback only, with no site-specific/private-LAN literal.
require_line debian/darkstat-nw.default 'BINDADDR="127.0.0.1"'
require_line debian/darkstat-nw.default "PORT=1667"
if grep -Eq '^[[:space:]]*BINDADDR=.*(192\.168\.|(^|[^0-9])10\.[0-9]|172\.(1[6-9]|2[0-9]|3[01])\.)' debian/darkstat-nw.default; then
  echo "default BINDADDR must not contain a private-LAN address" >&2
  exit 1
fi
if [ "$(grep -c '^BINDADDR=' debian/darkstat-nw.default)" != 1 ]; then
  echo "expected exactly one active BINDADDR line" >&2
  exit 1
fi
