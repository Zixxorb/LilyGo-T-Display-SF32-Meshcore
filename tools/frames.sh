#!/bin/bash
# Recent frames on the companion (debug builds: SF32_REMOTE_TOUCH=1): tools/frames.sh [COUNT]
# Prints, oldest first, the ms since the frame before and the ms to draw it.
cd "$(dirname "$0")/.."
source ./env.sh >/dev/null 2>&1
port=$(readlink -f $(ls /dev/serial/by-id/*1a86* 2>/dev/null | head -1) 2>/dev/null)
[ -n "$port" ] || { echo "board not found"; exit 1; }
exec python3 -I - "$port" "${1:-16}" <<'PY'
import sys, time, serial, subprocess
sys.path.insert(0, "tools")
import sf32_debug_uart as d
syms = {}
for l in subprocess.check_output(["arm-none-eabi-nm", ".flashed/main.elf"], text=True).splitlines():
    p = l.split()
    if len(p) == 3 and p[2].startswith("sf32_"): syms[p[2]] = int(p[0], 16)
if "sf32_gaps" not in syms: sys.exit("not a debug build (SF32_REMOTE_TOUCH=1)")
s = serial.Serial(sys.argv[1], 115200, timeout=0.05); s.rts = False
d.command(s, d.EXIT); time.sleep(0.3)
if d.command(s, d.ENTER)[:1] != b"\xd1": sys.exit("debug channel not answering")
n = d.mem_read32(s, syms["sf32_frame_n"])
g = [d.mem_read32(s, syms["sf32_gaps"] + 4 * i) for i in range(32)]
r = [d.mem_read32(s, syms["sf32_draws"] + 4 * i) for i in range(32)]
d.command(s, d.EXIT)
c = int(sys.argv[2])
print("frames", n)
print("gap :", [g[(n + i) & 31] for i in range(32)][-c:])
print("draw:", [r[(n + i) & 31] for i in range(32)][-c:])
PY
