#!/bin/sh
# Run uvc_pass (web page + audio) at every boot as a systemd service.
#   sudo ./install-service.sh           install and start
#   sudo ./install-service.sh --remove  stop and uninstall
set -e

SERVICE=uvc-voicechanger
UNIT=/etc/systemd/system/$SERVICE.service

if [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo: sudo ./install-service.sh"
    exit 1
fi

if [ "$1" = "--remove" ]; then
    systemctl disable --now "$SERVICE" 2>/dev/null || true
    rm -f "$UNIT"
    systemctl daemon-reload
    echo "Removed $SERVICE."
    exit 0
fi

RUN_USER="${SUDO_USER:-}"
if [ -z "$RUN_USER" ] || [ "$RUN_USER" = "root" ]; then
    echo "Run this as your normal user with sudo (not as root directly)."
    exit 1
fi

DIR="$(cd "$(dirname "$0")" && pwd)"
if [ ! -x "$DIR/uvc_pass" ]; then
    echo "Build first: cd $DIR && make"
    exit 1
fi

cat > "$UNIT" <<EOF
[Unit]
Description=UVC VoiceChanger (web config + audio)
After=sound.target network-online.target
Wants=network-online.target

[Service]
User=$RUN_USER
SupplementaryGroups=audio
WorkingDirectory=$DIR
ExecStart=$DIR/uvc_pass
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable "$SERVICE"
systemctl restart "$SERVICE"
echo "Installed $SERVICE: uvc_pass now starts at boot."
echo "  status : systemctl status $SERVICE"
echo "  logs   : journalctl -u $SERVICE -f"
echo "  after rebuilding: sudo systemctl restart $SERVICE"
