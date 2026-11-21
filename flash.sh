#!/usr/bin/env bash
# Flash over the CH343P USB-UART.   ./flash.sh [repeater|room_server|companion|selftest] [/dev/ttyACM0]
#
# sftool always connects at 1 Mbaud, but the firmware runs the console at 115200
# (MeshCore's baud). tools/sf32_debug_uart.py uses the chip's debug channel at the
# current baud to halt the CPU and switch the UART to 1 Mbaud, and then sftool
# works as usual.
set -e
cd "$(dirname "$0")"
target="${1:-repeater}"
case "$target" in
  repeater|selftest) shift || true; dir=project ;;
  companion) shift || true; dir=project_companion ;;
  room_server) shift || true; dir=project_room_server ;;
  *) target=repeater; dir=project ;;
esac
source ./env.sh
port="${1:-$(readlink -f $(ls /dev/serial/by-id/*1a86* 2>/dev/null | head -n1) 2>/dev/null)}"
[ -n "$port" ] || { echo "No serial port found; pass one, e.g. ./flash.sh $target /dev/ttyACM0"; exit 1; }
[ -f "$dir/build_t-display-sf32_hcpu/main.bin" ] || { echo "Not built yet: ./build.sh $target"; exit 1; }

# The board's RAM (test logs, sleep counters, battery history) doesn't survive a flash: save it
# first (tools/savelogs.sh, into board-logs/). If it can't be read, stop: SKIP_SAVELOGS=1 to
# flash anyway, e.g. a board that's crashed or has never been flashed from here.
if [ -z "$SKIP_SAVELOGS" ]; then
  tools/savelogs.sh preflash "$port" || {
    echo "Not flashing: the board's logs weren't saved. SKIP_SAVELOGS=1 ./flash.sh $target to flash anyway."
    exit 1
  }
fi

echo "Flashing $target ($dir) to $port"

python -I tools/sf32_debug_uart.py "$port" download 115200 \
  || python -I tools/sf32_debug_uart.py "$port" download 1000000 \
  || echo "Debug channel not answering at 115200 or 1M; trying sftool directly."

cd "$dir/build_t-display-sf32_hcpu"
sftool -p "$port" -c SF32LB52 -m nor write_flash \
  "bootloader/bootloader.bin@0x12010000" "main.bin@0x12020000" "ftab/ftab.bin@0x12000000"

# Keep this firmware's ELF: the next save reads the board with it, even after new builds.
cd ../..
mkdir -p .flashed
cp "$dir/build_t-display-sf32_hcpu/main.elf" .flashed/main.elf
cp "$dir/build_t-display-sf32_hcpu/main.bin" .flashed/main.bin  # (sf32_debug_uart.py mem: flash addresses)
echo "$target" > .flashed/target
