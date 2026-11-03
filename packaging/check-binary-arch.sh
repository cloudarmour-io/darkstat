#!/bin/sh
# Refuse to package a darkstat binary whose CPU architecture does not match the package architecture.
#
# WHY: nfpm only labels the package (GOARCH=amd64 -> "Architecture: amd64"); it never looks inside the binary. A stale
# cross-compiled ./darkstat (aarch64, left in the tree by an earlier arm64 build) was packaged into an amd64 .deb, which
# installed cleanly and then failed at runtime with "Exec format error", leaving the service in a restart loop.
#
# usage: packaging/check-binary-arch.sh <binary> <amd64|arm64>
set -eu
bin="${1:-}"; arch="${2:-}"
[ -n "$bin" ] && [ -n "$arch" ] || { echo "usage: $0 <binary> <amd64|arm64>" >&2; exit 2; }
[ -f "$bin" ] || { echo "check-binary-arch: $bin does not exist (build it first, see packaging/README.md)" >&2; exit 1; }
case "$arch" in
  amd64) want='x86-64' ;;
  arm64) want='aarch64' ;;
  *) echo "check-binary-arch: unsupported architecture '$arch' (use amd64 or arm64)" >&2; exit 2 ;;
esac
if command -v file >/dev/null 2>&1; then
  info=$(file -bL "$bin")   # -L: follow a symlink to the real file
else
  info=$(readelf -h "$bin" 2>/dev/null | sed -n 's/^ *Machine: *//p')
fi
# "file" says "x86-64" or "ARM aarch64"; readelf says "Advanced Micro Devices X86-64" or "AArch64"
lowered=$(printf '%s' "$info" | tr 'A-Z' 'a-z')
case "$lowered" in
  *"$want"*) exit 0 ;;
esac
echo "check-binary-arch: $bin is not a $arch binary (found: $info). Rebuild it for $arch before packaging." >&2
exit 1
