#!/bin/bash
# Read the companion's battery sleep counters (sf32_pm_stats, SF32Board.cpp) after a run on
# battery: plug USB back in (that freezes them), then run this. The ELF must be the flashed build.
cd "$(dirname "$0")/.."
source ./env.sh >/dev/null 2>&1
addr=$(arm-none-eabi-nm project_companion/build_t-display-sf32_hcpu/main.elf | awk '/ sf32_pm_stats$/{print $1}')
[ -n "$addr" ] || { echo "no sf32_pm_stats in this build"; exit 1; }
port=$(readlink -f $(ls /dev/serial/by-id/*1a86* 2>/dev/null | head -1) 2>/dev/null)
[ -n "$port" ] || { echo "board not found"; exit 1; }
python3 tools/sf32_debug_uart.py "$port" mem "$addr" 24 | python3 -I -c '
import sys, struct
w = [int(l.split()[1], 16) for l in sys.stdin if ":" in l]
if len(w) < 24 or w[0] != 0x5346504D: sys.exit("no counts yet (has it run on battery?)")
batt, slept, sleeps, held, loops, mask = w[1:7]
wsr = w[7:24]
h = batt / 3600000
print(f"on battery {batt/60000:.1f} min, asleep {100*slept/max(batt,1):.1f}%, "
      f"held awake {100*held/max(batt,1):.1f}% (reasons seen {mask:#x}: 1 boot 2 screen 4 audio 8 user 16 usb)")
print(f"wakes {sleeps} ({sleeps/max(batt/1000,1):.1f}/s), main loop {loops/max(batt/1000,1):.1f}/s")
names = ["RTC", "GPIO1", "LPTIM1", "PMUC", "b4", "b5", "LP2HP req", "LP2HP irq"] + [f"PIN{i}" for i in range(8)] + ["other"]
print("wake sources: " + ", ".join(f"{n} {c}" for n, c in zip(names, wsr) if c))
'
