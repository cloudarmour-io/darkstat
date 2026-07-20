#!/bin/sh
set -e

# Only on full purge/erase
case "${1:-}" in
  purge|0)
    rm -rf /var/lib/darkstat

    if getent passwd darkstat >/dev/null 2>&1; then
      userdel darkstat >/dev/null 2>&1 || true
    fi
    if getent group darkstat >/dev/null 2>&1; then
      groupdel darkstat >/dev/null 2>&1 || true
    fi
    ;;
esac

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
  systemctl daemon-reload >/dev/null 2>&1 || true
  systemctl reset-failed darkstat >/dev/null 2>&1 || true
fi
