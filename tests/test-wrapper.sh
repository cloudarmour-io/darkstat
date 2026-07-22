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

require_line debian/darkstat-nw-wrapper ': "${HOSTS_MAX:=1000}"'
require_line debian/darkstat-nw-wrapper ': "${HOSTS_KEEP:=500}"'
require_line debian/darkstat-nw-wrapper ': "${PORTS_MAX:=60}"'
require_line debian/darkstat-nw-wrapper ': "${PORTS_KEEP:=30}"'
require_line debian/darkstat-nw-wrapper ': "${HIGHEST_PORT:=65535}"'
require_line debian/darkstat-nw-wrapper ': "${HOST_RETENTION_HOURS:=168}"'
require_line debian/darkstat-nw-wrapper ': "${MEM_LIMIT_MB:=0}"'
require_line debian/darkstat-nw-wrapper ': "${API_KEY:=}"'

require_line debian/darkstat-nw-wrapper '  set -- "$@" --hosts-max "${HOSTS_MAX}"'
require_line debian/darkstat-nw-wrapper '  set -- "$@" --hosts-keep "${HOSTS_KEEP}"'
require_line debian/darkstat-nw-wrapper '  set -- "$@" --ports-max "${PORTS_MAX}"'
require_line debian/darkstat-nw-wrapper '  set -- "$@" --ports-keep "${PORTS_KEEP}"'
require_line debian/darkstat-nw-wrapper '  set -- "$@" --highest-port "${HIGHEST_PORT}"'
require_line debian/darkstat-nw-wrapper '  set -- "$@" --host-retention-hours "${HOST_RETENTION_HOURS}"'
require_line debian/darkstat-nw-wrapper '  set -- "$@" --mem-limit-mb "${MEM_LIMIT_MB}"'
require_line debian/darkstat-nw-wrapper '  set -- "$@" --api-key-md5 "${API_KEY}"'
