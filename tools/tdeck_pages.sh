#!/bin/bash
# T-Deck (debug firmware): screenshots of every home page into shots/DIR/, as grids of six.
#   tools/tdeck_pages.sh DIR
cd "$(dirname "$0")/../tdeck"
out=../shots/${1:-pages}; mkdir -p "$out"
python3 -I remote.py press . shot "$out/p0.png" > /dev/null || exit 1
for i in $(seq 1 13); do python3 -I remote.py left . shot "$out/p$i.png" > /dev/null; done
python3 - "$out" <<'PY'
import sys, glob
from PIL import Image
d = sys.argv[1]
ims = [Image.open(f"{d}/p{i}.png") for i in range(14)]
for k in range(0, 14, 6):
    g = Image.new("RGB", (3 * 330, 2 * 250), (90, 90, 90))
    for j, im in enumerate(ims[k:k + 6]): g.paste(im, ((j % 3) * 330, (j // 3) * 250))
    g.save(f"{d}/grid{k // 6}.png")
PY
echo "$out/grid0.png .. grid2.png"
