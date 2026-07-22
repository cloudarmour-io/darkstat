#!/bin/sh
set -e

if ! getent group darkstat-nw >/dev/null 2>&1; then
  groupadd --system darkstat-nw
fi

if ! getent passwd darkstat-nw >/dev/null 2>&1; then
  useradd --system \
    --gid darkstat-nw \
    --home-dir /var/lib/darkstat-nw \
    --no-create-home \
    --shell /usr/sbin/nologin \
    --comment "darkstat-nw daemon user" \
    darkstat-nw
fi

install -d -o darkstat-nw -g darkstat-nw -m 0750 /var/lib/darkstat-nw

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
  systemctl daemon-reload >/dev/null 2>&1 || true
  systemctl enable darkstat-nw >/dev/null 2>&1 || true
fi

# Detect a suggested interface for the hint message
SUGGESTED=$(ip route show default 2>/dev/null | awk '/^default/ { print $5; exit }')

echo ""
echo "┌─────────────────────────────────────────────────────┐"
echo "│              darkstat-nw installed                   │"
echo "└─────────────────────────────────────────────────────┘"
echo ""
if [ -n "${SUGGESTED}" ]; then
  sed -i "s/^INTERFACES=$/INTERFACES=${SUGGESTED}/" /etc/default/darkstat-nw
  echo "  Interface auto-detected and set: ${SUGGESTED}"
  echo "  To monitor additional interfaces, edit the config:"
  echo ""
  echo "    sudoedit /etc/default/darkstat-nw   # e.g. INTERFACES=\"eth0 eth1\""
  echo ""
  echo "  Then start the service:"
else
  echo "  Could not auto-detect interface. Edit the config first:"
  echo ""
  echo "    sudoedit /etc/default/darkstat-nw   # set INTERFACES=\"eth0 eth1\""
  echo ""
  echo "  Then start the service:"
fi
echo ""
echo "    sudo systemctl start darkstat-nw"
echo "    sudo systemctl status darkstat-nw"
echo ""
echo "  Web UI will be available at:  http://localhost:1667"
echo ""
