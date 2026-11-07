#!/bin/bash
# Save everything the board holds in RAM (test log, sleep counters, battery history, settings)
# to board-logs/DATE_TIME[_LABEL]/, raw and decoded. flash.sh runs this first, because flashing
# or a reboot loses it; run it by hand with a label after a test: tools/savelogs.sh sleep_test
#
# It reads with the ELF of the firmware actually on the board (.flashed/, kept by flash.sh), since
# a new build moves the addresses. Exit 0 = saved, 1 = nothing could be read.
cd "$(dirname "$0")/.."
source ./env.sh >/dev/null 2>&1
elf=.flashed/main.elf
[ -f "$elf" ] || { echo "savelogs: no $elf (nothing flashed by flash.sh yet)"; exit 1; }
port="${2:-$(readlink -f $(ls /dev/serial/by-id/*1a86* 2>/dev/null | head -1) 2>/dev/null)}"
[ -n "$port" ] || { echo "savelogs: board not found"; exit 1; }

out="board-logs/$(date +%Y-%m-%d_%H%M)${1:+_$1}"
mkdir -p "$out"
arm-none-eabi-nm -S "$elf" > "$out/symbols.txt"   # so the raw dumps can be decoded later
cp .flashed/target "$out/target.txt" 2>/dev/null
sha256sum "$elf" > "$out/elf.sha256"

# name, symbol: raw words of every data block worth keeping, sized from the ELF
ok=0
while read -r sym; do
  line=$(awk -v s="$sym" '$NF == s && NF == 4 {print $1, $2; exit}' "$out/symbols.txt")
  [ -n "$line" ] || continue
  set -- $line
  words=$(( (0x$2 + 3) / 4 ))
  if python3 tools/sf32_debug_uart.py "$port" mem "$1" "$words" > "$out/$sym.txt" 2>&1 \
     && grep -q ':' "$out/$sym.txt"; then
    ok=$((ok + 1))
  else
    rm -f "$out/$sym.txt"
  fi
done <<'EOF'
sf32_pm_stats
sf32_ab_log
_ZL12batt_history
ui_task
EOF
if [ "$ok" = 0 ]; then
  rm -rf "$out"
  echo "savelogs: the board didn't answer (is it running? a crashed board can't be read)"
  exit 1
fi

# the readable versions too, if this build has them (they read the same ELF)
ELF="$elf" tools/pmstats.sh > "$out/pmstats.txt" 2>&1
ELF="$elf" tools/abstats.sh > "$out/abstats.txt" 2>&1
echo "savelogs: saved $ok blocks to $out"
