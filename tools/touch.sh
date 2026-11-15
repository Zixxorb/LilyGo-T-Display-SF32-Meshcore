#!/bin/bash
# Remote touch on the companion (checking the UI from a PC; the board must be awake: on USB).
# Debug builds only: SF32_REMOTE_TOUCH=1 ./build.sh companion (normal firmware has no hook).
#   tools/touch.sh tap X Y     tap at panel pixel X,Y (0..479)
#   tools/touch.sh up|down|left|right   swipe
#   tools/touch.sh press       button-like press (wakes the screen)
#   tools/touch.sh hold X Y MS  finger held still at X,Y for MS milliseconds, then lifted
#   tools/touch.sh drag X0 Y X1 MS  finger dragged sideways from X0 to X1 over MS milliseconds
#   tools/touch.sh vdrag X Y0 Y1 MS finger dragged up or down (scrolls a list)
# Several steps in one call: tools/touch.sh up . tap 240 300 . down   ("." = wait 1.2 s)
# Writes SF32TouchInput's sf32_remote_touch over the debug channel (see SF32TouchInput.cpp).
cd "$(dirname "$0")/.."
source ./env.sh >/dev/null 2>&1
addr=$(arm-none-eabi-nm project_companion/build_t-display-sf32_hcpu/main.elf | awk '/ sf32_remote_touch$/{print $1}')
[ -n "$addr" ] || { echo "no remote touch in this build: SF32_REMOTE_TOUCH=1 ./build.sh companion (debug only)"; exit 1; }
port=$(readlink -f $(ls /dev/serial/by-id/*1a86* 2>/dev/null | head -1) 2>/dev/null)
[ -n "$port" ] || { echo "board not found"; exit 1; }
exec python3 -I - "$port" "$addr" "$@" <<'EOF'
import sys, time, serial
sys.path.insert(0, "tools")
import sf32_debug_uart as d
port, addr, args = sys.argv[1], int(sys.argv[2], 16), sys.argv[3:]
p = serial.Serial(port, 115200, timeout=0.05)
p.rts = False
d.command(p, d.EXIT); time.sleep(0.3)
if d.command(p, d.ENTER)[:1] != b"\xd1":
    sys.exit("debug channel not answering")
codes = {"up": 2, "down": 3, "left": 4, "right": 5, "press": 6}
i = 0
while i < len(args):
    a = args[i]
    if a == ".":
        time.sleep(1.2); i += 1; continue
    if a == "hold":
        x, y, ms = int(args[i + 1]), int(args[i + 2]), int(args[i + 3]); i += 4
        for v, wait in (((7 << 24) | (x << 12) | y, ms / 1000.0), (8 << 24, 0.2)):
            ok = any(d.mem_write32(p, addr, v)[:1] == b"\xd3" or time.sleep(0.05) for _ in range(20))
            time.sleep(wait)
        print("hold sent")
        continue
    if a == "drag":  # drag X0 Y X1 MS: finger down, across in steps, lifts
        x0, y, x1, ms = int(args[i + 1]), int(args[i + 2]), int(args[i + 3]), int(args[i + 4]); i += 5
        steps = max(abs(x1 - x0) // 20, 1)
        seq = [((7 << 24) | (x0 << 12) | y, ms / 1000.0 / steps)]
        seq += [((9 << 24) | ((x0 + (x1 - x0) * k // steps) << 12) | y, ms / 1000.0 / steps) for k in range(1, steps + 1)]
        seq += [(8 << 24, 0.3)]
        for v, wait in seq:
            any(d.mem_write32(p, addr, v)[:1] == b"\xd3" or time.sleep(0.02) for _ in range(20))
            time.sleep(wait)
        print("drag sent")
        continue
    if a == "vdrag":  # vdrag X Y0 Y1 MS: finger down, up or down in steps, lifts (scroll lists)
        x, y0, y1, ms = int(args[i + 1]), int(args[i + 2]), int(args[i + 3]), int(args[i + 4]); i += 5
        steps = max(abs(y1 - y0) // 20, 1)
        seq = [((7 << 24) | (x << 12) | y0, ms / 1000.0 / steps)]
        seq += [((9 << 24) | (x << 12) | (y0 + (y1 - y0) * k // steps), ms / 1000.0 / steps) for k in range(1, steps + 1)]
        seq += [(8 << 24, 0.3)]
        for v, wait in seq:
            any(d.mem_write32(p, addr, v)[:1] == b"\xd3" or time.sleep(0.02) for _ in range(20))
            time.sleep(wait)
        print("vdrag sent")
        continue
    if a == "tap":
        x, y = int(args[i + 1]), int(args[i + 2]); i += 3
        v = (1 << 24) | (x << 12) | y
    else:
        v = codes[a] << 24; i += 1
    ok = False
    for _ in range(20):
        if d.mem_write32(p, addr, v)[:1] == b"\xd3":
            ok = True; break
        time.sleep(0.05)
    print(a, "sent" if ok else "FAILED (asleep?)")
    time.sleep(0.4)
p.write(d.frame(d.EXIT))
EOF
