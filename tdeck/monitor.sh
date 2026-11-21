#!/usr/bin/env bash
# The T-Deck's USB serial console.   ./monitor.sh [/dev/ttyACM1]
cd "$(dirname "$0")"
source ./port.sh
pio=$(command -v pio || echo ~/.platformio/penv/bin/pio)
port=$(tdeck_port "$1") || { echo "T-Deck not found (USB)"; exit 1; }
exec "$pio" device monitor -p "$port" -b 115200
