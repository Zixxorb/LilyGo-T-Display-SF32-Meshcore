#!/usr/bin/env python3
"""Screenshot the T-Deck (debug builds: ./build.sh -e tdeck_debug).   shot.py OUT.png [PORT]"""
import glob, os, sys, time
import serial
from PIL import Image

out = sys.argv[1]
port = sys.argv[2] if len(sys.argv) > 2 else os.path.realpath(glob.glob("/dev/serial/by-id/usb-Espressif*")[0])
s = serial.Serial(port, 115200, timeout=3)
s.reset_input_buffer()
s.write(b"shot\n")
deadline = time.time() + 5
while time.time() < deadline:
    line = s.readline().decode("ascii", "replace").strip()
    if line.startswith("SHOT "):
        break
else:
    sys.exit("no answer: is it a debug build (tdeck_debug)?")
w, h = map(int, line.split()[1:3])
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
