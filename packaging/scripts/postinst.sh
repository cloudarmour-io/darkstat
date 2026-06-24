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
echo "  Network interface is auto-detected from the default route."
echo "  To override, edit the config:"
echo ""
echo "    sudoedit /etc/default/darkstat"
echo ""
if [ -n "${SUGGESTED}" ]; then
echo "  Detected interface: ${SUGGESTED}"
echo "  Set:  INTERFACE=${SUGGESTED}"
echo ""
fi
echo "  Then start the service:"
echo ""
echo "    sudo systemctl daemon-reload"
echo "    sudo systemctl start darkstat"
echo "    sudo systemctl status darkstat"
echo ""
echo "  Web UI will be available at:  http://localhost:667"
echo ""
