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

# Detect a suggested interface for the hint message
SUGGESTED=$(ip route show default 2>/dev/null | awk '/^default/ { print $5; exit }')

echo ""
echo "┌─────────────────────────────────────────────────────┐"
echo "│              darkstat installed                     │"
echo "└─────────────────────────────────────────────────────┘"
echo ""
if [ -n "${SUGGESTED}" ]; then
  sed -i "s/^INTERFACE=$/INTERFACE=${SUGGESTED}/" /etc/default/darkstat
  echo "  Interface auto-detected and set: ${SUGGESTED}"
  echo "  No configuration needed — just start the service:"
else
  echo "  Could not auto-detect interface. Edit the config first:"
  echo ""
  echo "    sudoedit /etc/default/darkstat   # set INTERFACE=<your interface>"
  echo ""
  echo "  Then start the service:"
fi
echo ""
echo "    sudo systemctl start darkstat"
echo "    sudo systemctl status darkstat"
echo ""
echo "  Web UI will be available at:  http://localhost:667"
echo ""
