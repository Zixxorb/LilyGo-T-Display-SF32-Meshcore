#!/usr/bin/env bash
# Flash the T-Deck.   ./flash.sh [/dev/ttyACM1]
#
# First it saves the T-Deck's data partition (SPIFFS: identity, contacts, channels, settings)
# to ../board-logs/ (save_data.py), like the SF32's savelogs: flashing doesn't erase it, but a
# bad build could. The copy holds the node's private key: board-logs/ is never committed.
# SKIP_SAVELOGS=1 skips it.
# If the T-Deck doesn't show up (a crashing build): hold the trackball down, press reset.
set -e
cd "$(dirname "$0")"
source ./port.sh
pio=$(command -v pio || echo ~/.platformio/penv/bin/pio)
port=$(tdeck_port "$1") || { echo "T-Deck not found (USB). Pass the port: ./flash.sh /dev/ttyACM1"; exit 1; }
[ -f .pio/build/tdeck_companion/firmware.bin ] || { echo "Not built yet: ./build.sh"; exit 1; }

if [ -z "$SKIP_SAVELOGS" ]; then
  python3 -I save_data.py "$port" "../board-logs/$(date +%Y-%m-%d_%H%M)_tdeck_preflash" || {
    echo "Not flashing: the data partition wasn't saved. SKIP_SAVELOGS=1 ./flash.sh to flash anyway."
    exit 1
  }
fi

# bootloader, partition table, boot_app0 and the app only: the data partition isn't written
"$pio" run -t upload --upload-port "$port"
