#!/bin/bash
# ============================================================
#  install-status-daemon.sh
#  Deploie le daemon ES WiFi/BT et les hooks evenements
#  A lancer depuis le dossier scripts/ du repo
#  Usage: sudo bash scripts/install-status-daemon.sh
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DAEMON_SCRIPT="/usr/local/bin/es-status-daemon.sh"
DAEMON_SERVICE="/etc/systemd/system/es-status-daemon.service"

GREEN="\e[32m"; ORANGE="\e[33m"; RED="\e[31m"; RESET="\e[0m"
print_ok()   { echo -e "  ${GREEN}[OK]${RESET}    $1"; }
print_warn() { echo -e "  ${ORANGE}[WARN]${RESET}  $1"; }
print_err()  { echo -e "  ${RED}[ERR]${RESET}    $1"; }

echo ""
echo "=================================================="
echo "   ES Status Daemon -- Install"
echo "=================================================="
echo ""

# ---- Daemon script ----
sudo cp "$SCRIPT_DIR/es-status-daemon.sh" "$DAEMON_SCRIPT"
sudo chmod +x "$DAEMON_SCRIPT"
print_ok "Daemon script -> $DAEMON_SCRIPT"

# ---- Systemd service ----
sudo cp "$SCRIPT_DIR/es-status-daemon.service" "$DAEMON_SERVICE"
sudo systemctl daemon-reload
sudo systemctl enable es-status-daemon.service
sudo systemctl restart es-status-daemon.service
print_ok "Service enabled and started"

# ---- NetworkManager dispatcher -- instant WiFi detection ----
NM_DISPATCH="/etc/NetworkManager/dispatcher.d/99-es-wifi-icon"
if [ ! -f "$NM_DISPATCH" ]; then
    sudo tee "$NM_DISPATCH" > /dev/null << 'NMEOF'
#!/bin/bash
touch /tmp/es-wifi-changed
NMEOF
    sudo chmod +x "$NM_DISPATCH"
    print_ok "NetworkManager dispatcher installed"
else
    print_ok "NetworkManager dispatcher already present"
fi

# ---- Systemd drop-ins -- instant SSH/Samba/filebrowser detection ----
for SERVICE in ssh smbd nmbd filebrowser; do
    DROP_DIR="/etc/systemd/system/${SERVICE}.service.d"
    DROP_FILE="$DROP_DIR/es-icon.conf"
    if systemctl list-unit-files "${SERVICE}.service" > /dev/null 2>&1; then
        if [ ! -f "$DROP_FILE" ]; then
            sudo mkdir -p "$DROP_DIR"
            sudo tee "$DROP_FILE" > /dev/null << DROPIN
[Service]
ExecStartPost=/bin/sh -c 'touch /tmp/es-wifi-changed'
ExecStopPost=/bin/sh -c 'touch /tmp/es-wifi-changed'
DROPIN
            print_ok "Drop-in installed: $SERVICE"
        else
            print_ok "Drop-in already present: $SERVICE"
        fi
    fi
done
sudo systemctl daemon-reload 2>/dev/null

# ---- udev rules -- instant BT + backup WiFi detection ----
UDEV_RULE="/etc/udev/rules.d/99-es-icons.rules"
if [ ! -f "$UDEV_RULE" ]; then
    sudo tee "$UDEV_RULE" > /dev/null << 'UDEVEOF'
SUBSYSTEM=="net",       KERNEL=="wlan0",  ACTION=="add",    RUN+="/bin/sh -c 'touch /tmp/es-wifi-changed'"
SUBSYSTEM=="net",       KERNEL=="wlan0",  ACTION=="remove", RUN+="/bin/sh -c 'touch /tmp/es-wifi-changed'"
SUBSYSTEM=="bluetooth", ACTION=="add",    RUN+="/bin/sh -c 'touch /tmp/es-bt-changed'"
SUBSYSTEM=="bluetooth", ACTION=="remove", RUN+="/bin/sh -c 'touch /tmp/es-bt-changed'"
UDEVEOF
    sudo udevadm control --reload-rules
    print_ok "udev rules installed"
else
    print_ok "udev rules already present"
fi

echo ""
echo "=================================================="
echo "  Done! WiFi: 0=off 1=no-ip 2=ok 3=sharing 4=svc"
echo "  BT:         0=off 1=active 2=connected"
echo "=================================================="
echo ""
