#!/bin/bash
# ============================================================
#  install-zram-swap.sh
#  Deploy zram-swap service and setup script
#  Run from the repo's scripts/ directory
#  Usage: sudo bash scripts/install-zram-swap.sh
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SETUP_SCRIPT="/usr/local/bin/zram-setup.sh"
SERVICE_FILE="/etc/systemd/system/zram-swap.service"

GREEN="\e[32m"; RED="\e[31m"; RESET="\e[0m"
print_ok()  { echo -e "  ${GREEN}[OK]${RESET}    $1"; }
print_err() { echo -e "  ${RED}[ERR]${RESET}   $1"; }

echo ""
echo "=================================================="
echo "   ZRAM Swap Service -- Install"
echo "=================================================="
echo ""

# ---- Setup script ----
sudo cp "$SCRIPT_DIR/zram-setup.sh" "$SETUP_SCRIPT"
sudo chmod +x "$SETUP_SCRIPT"
print_ok "Setup script -> $SETUP_SCRIPT"

# ---- Systemd service ----
sudo cp "$SCRIPT_DIR/zram-swap.service" "$SERVICE_FILE"
sudo systemctl daemon-reload
print_ok "Service installed -> $SERVICE_FILE"

# ---- Create default config if not present ----
if [ ! -f /etc/zram.conf ]; then
    echo -e "ENABLED=0\nALGORITHM=lz4\nSIZE=536870912" | sudo tee /etc/zram.conf >/dev/null
    print_ok "Default config created -> /etc/zram.conf (disabled)"
else
    print_ok "Config already present -> /etc/zram.conf"
fi

echo ""
echo "=================================================="
echo "  Done! Configure via ES: ZRAM SETTINGS menu"
echo "  Or edit /etc/zram.conf and enable the service."
echo "=================================================="
echo ""
