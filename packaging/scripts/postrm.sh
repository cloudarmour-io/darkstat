#!/bin/sh
set -e

# Only on full purge/erase
case "${1:-}" in
  purge|0)
    rm -rf /var/lib/darkstat-nw

    if getent passwd darkstat-nw >/dev/null 2>&1; then
      userdel darkstat-nw >/dev/null 2>&1 || true
    fi
    if getent group darkstat-nw >/dev/null 2>&1; then
      groupdel darkstat-nw >/dev/null 2>&1 || true
    fi
    ;;
esac

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
  systemctl daemon-reload >/dev/null 2>&1 || true
  systemctl reset-failed darkstat-nw >/dev/null 2>&1 || true
fi
