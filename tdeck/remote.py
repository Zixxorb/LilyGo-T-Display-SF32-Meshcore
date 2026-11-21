#!/usr/bin/env python3
"""Look at and drive the T-Deck's UI from the PC (debug firmware only: ./build.sh -e tdeck_debug,
ENV=tdeck_debug ./flash.sh). Steps run in order:
  shot FILE.png          screenshot
  tap X Y                tap at panel pixel X,Y (320x240)
  up|down|left|right     swipe
  press                  button-like press (wakes the screen)
  hold X Y MS            finger held still at X,Y for MS ms, then lifted
  drag X0 Y X1 MS        finger dragged sideways
  vdrag X Y0 Y1 MS       finger dragged up or down (scrolls a list)
  fakemsg | fakemeet HEX a pretend message / nearby Starlit
  .                      wait 1.2 s
e.g. remote.py press . up . shot ../shots/menu.png"""
import glob, os, sys, time
import serial
from PIL import Image

args = sys.argv[1:]
if not args:
    sys.exit(__doc__)
ports = glob.glob("/dev/serial/by-id/usb-Espressif*")
if len(ports) != 1:
    sys.exit("T-Deck not found (or more than one Espressif board)")
s = serial.Serial(os.path.realpath(ports[0]), 115200, timeout=3)
s.reset_input_buffer()


def answer(prefix, timeout=3):
    end = time.time() + timeout
    while time.time() < end:
        line = s.readline().decode("ascii", "replace").strip()
        if line.startswith(prefix):
            return line
    sys.exit(f"no answer: is it the debug firmware (tdeck_debug)?")


def rt(v, wait=0.0):
    s.write(b"rt %x\n" % v)
    answer("OK")
    time.sleep(wait)


def shot(out):
    s.reset_input_buffer()
    s.write(b"shot\n")
    w, h = map(int, answer("SHOT ").split()[1:3])
    data = s.read(w * h * 2)
    if len(data) != w * h * 2:
        sys.exit(f"short read: {len(data)} of {w * h * 2} bytes")
    px = []
    for i in range(0, len(data), 2):
        v = data[i] | data[i + 1] << 8
        px.append((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    im = Image.new("RGB", (w, h))
    im.putdata(px)
    im.save(out)
    print("saved", out)


def path(x0, y0, x1, y1, ms):
    steps = max(max(abs(x1 - x0), abs(y1 - y0)) // 12, 1)
    rt((7 << 24) | (x0 << 12) | y0, ms / 1000.0 / steps)
    for k in range(1, steps + 1):
        rt((9 << 24) | ((x0 + (x1 - x0) * k // steps) << 12) | (y0 + (y1 - y0) * k // steps), ms / 1000.0 / steps)
    rt(8 << 24, 0.3)


codes = {"up": 2, "down": 3, "left": 4, "right": 5, "press": 6}
i = 0
while i < len(args):
    a = args[i]
    if a == ".":
        time.sleep(1.2); i += 1
    elif a == "shot":
        shot(args[i + 1]); i += 2
    elif a == "tap":
        x, y = int(args[i + 1]), int(args[i + 2]); i += 3
        rt((1 << 24) | (x << 12) | y, 0.4)
    elif a == "hold":
        x, y, ms = int(args[i + 1]), int(args[i + 2]), int(args[i + 3]); i += 4
        rt((7 << 24) | (x << 12) | y, ms / 1000.0)
        rt(8 << 24, 0.3)
    elif a == "drag":
        x0, y, x1, ms = map(int, args[i + 1:i + 5]); i += 5
        path(x0, y, x1, y, ms)
    elif a == "vdrag":
        x, y0, y1, ms = map(int, args[i + 1:i + 5]); i += 5
        path(x, y0, x, y1, ms)
    elif a == "fakemsg":
        s.write(b"fakemsg\n"); answer("OK"); i += 1
    elif a == "fakemeet":
        s.write(b"fakemeet %s\n" % args[i + 1].encode()); answer("OK"); i += 2
    elif a in codes:
        rt(codes[a] << 24, 0.4); i += 1
    else:
        sys.exit(f"unknown step: {a}")
