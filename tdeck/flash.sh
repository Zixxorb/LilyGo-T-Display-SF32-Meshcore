#!/usr/bin/env bash
# Flash the T-Deck.   ./flash.sh [/dev/ttyACM1]   (debug firmware: ENV=tdeck_debug ./flash.sh)
#
# First it saves the T-Deck's data partition (SPIFFS: identity, contacts, channels, settings)
# to ../board-logs/ (save_data.py; at most once a day), like the SF32's savelogs: flashing doesn't erase it, but a
# bad build could. The copy holds the node's private key: board-logs/ is never committed.
# SKIP_SAVELOGS=1 skips it.
# If the T-Deck doesn't show up (a crashing build): hold the trackball down, press reset.
set -e
cd "$(dirname "$0")"
source ./port.sh
pio=$(command -v pio || echo ~/.platformio/penv/bin/pio)
port=$(tdeck_port "$1") || { echo "T-Deck not found (USB). Pass the port: ./flash.sh /dev/ttyACM1"; exit 1; }
env=${ENV:-tdeck_companion}
[ -f .pio/build/$env/firmware.bin ] || { echo "Not built yet: ./build.sh -e $env"; exit 1; }

if [ -z "$SKIP_SAVELOGS" ]; then
  python3 -I save_data.py "$port" "../board-logs/$(date +%Y-%m-%d_%H%M)_tdeck_preflash" || {
    echo "Not flashing: the data partition wasn't saved. SKIP_SAVELOGS=1 ./flash.sh to flash anyway."
    exit 1
  }
fi

# bootloader, partition table, boot_app0 and the app only: the data partition isn't written
"$pio" run -e "$env" -t upload --upload-port "$port"

# the normal firmware must not have the debug tools
if [ "$env" != tdeck_debug ] && grep -q tdeck_shot_poll <(~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm .pio/build/$env/firmware.elf); then
  echo "WARNING: the normal build has the screenshot tool in it"; exit 1
fi
