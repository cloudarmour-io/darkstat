#!/bin/sh
set -e

if ! getent group darkstat >/dev/null 2>&1; then
  groupadd --system darkstat
fi

if ! getent passwd darkstat >/dev/null 2>&1; then
  useradd --system \
    --gid darkstat \
    --home-dir /var/lib/darkstat \
    --no-create-home \
    --shell /usr/sbin/nologin \
    --comment "darkstat daemon user" \
    darkstat
fi

install -d -o darkstat -g darkstat -m 0750 /var/lib/darkstat

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
  systemctl daemon-reload >/dev/null 2>&1 || true
  systemctl enable darkstat >/dev/null 2>&1 || true
fi
