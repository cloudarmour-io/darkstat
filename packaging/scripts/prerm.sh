#!/bin/sh
set -e

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
  systemctl stop darkstat >/dev/null 2>&1 || true
  systemctl disable darkstat >/dev/null 2>&1 || true
fi
