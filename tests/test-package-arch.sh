#!/bin/sh
# Tests packaging/check-binary-arch.sh with a real system binary of the host architecture.
set -u
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
guard="$here/../packaging/check-binary-arch.sh"
sample=/bin/sh
case "$(uname -m)" in
  x86_64)  host=amd64; other=arm64 ;;
  aarch64) host=arm64; other=amd64 ;;
  *) echo "SKIP: unsupported host architecture $(uname -m)"; exit 0 ;;
esac
fail=0
expect() { # label expected-exit command...
  label="$1"; want="$2"; shift 2
  "$@" >/dev/null 2>&1; rc=$?
  if [ "$rc" -eq "$want" ]; then echo "PASS: $label"; else echo "FAIL: $label (exit $rc, wanted $want)" >&2; fail=1; fi
}
expect "a $host binary is accepted for $host"            0 "$guard" "$sample" "$host"
expect "a $host binary is REFUSED for $other"            1 "$guard" "$sample" "$other"
expect "a missing binary is refused"                     1 "$guard" /nonexistent/darkstat "$host"
expect "an unsupported architecture is a usage error"    2 "$guard" "$sample" mips
expect "missing arguments are a usage error"             2 "$guard"
printf 'not an elf file\n' > "${TMPDIR:-/tmp}/dk-arch-test.$$"
expect "a non-ELF file is refused"                       1 "$guard" "${TMPDIR:-/tmp}/dk-arch-test.$$" "$host"
rm -f "${TMPDIR:-/tmp}/dk-arch-test.$$"
exit $fail
