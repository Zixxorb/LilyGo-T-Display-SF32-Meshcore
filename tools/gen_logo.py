#!/usr/bin/env python3
# Builds docs/images/logo-dark.svg and logo-light.svg (README banner): the Vesper mark plus
# the VESPER wordmark, with the lettering drawn as outlines from Michroma (SIL OFL 1.1,
# licenses/OFL-Michroma.txt) so GitHub needs no web font.
# Usage: python3 tools/gen_logo.py Michroma-Regular.ttf docs/images
import sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen

font_path, out_dir = sys.argv[1], sys.argv[2]
f = TTFont(font_path)
gs = f.getGlyphSet(); cmap = f.getBestCmap(); upm = f["head"].unitsPerEm
SIZE, TRACK = 40.0, 0.3 * 40.0
sc = SIZE / upm
MARK_W, MARK_H, GAP = 168, 140, 26
cap = f["OS/2"].sCapHeight * sc
# text path, baseline at y so caps are centred on the mark
base = MARK_H / 2 + cap / 2
pen = SVGPathPen(gs)
x = MARK_W + GAP
text = "VESPER"
for i, ch in enumerate(text):
    g = cmap[ord(ch)]
    gs[g].draw(TransformPen(pen, (sc, 0, 0, -sc, x, base)))
    x += gs[g].width * sc + (TRACK if i < len(text) - 1 else 0)
W = int(x + 0.999); H = MARK_H
word = pen.getCommands()

def mark(pfx, sun, line, star):
    return f'''<g transform="scale(0.7)"><defs><mask id="{pfx}s" maskUnits="userSpaceOnUse" x="0" y="0" width="240" height="200"><rect width="240" height="200" fill="white"/><path d="M48 40 L90 40 L120 140 L150 40 L192 40 L138 160 L102 160 Z" fill="black" stroke="black" stroke-width="7" stroke-linejoin="round"/></mask><mask id="{pfx}l" maskUnits="userSpaceOnUse" x="0" y="0" width="240" height="200"><rect width="240" height="200" fill="white"/><rect x="0" y="104" width="240" height="32" fill="black"/></mask></defs><path d="M90 114 A30 30 0 0 1 150 114 Z" fill="{sun}" mask="url(#{pfx}s)"/><path d="M48 40 L90 40 L120 140 L150 40 L192 40 L138 160 L102 160 Z" fill="#9B3D8F"/><path d="M66 26 L26 26 L94 174 L146 174 L214 26 L174 26" fill="none" stroke="{line}" stroke-width="5" mask="url(#{pfx}l)"/><path d="M120 42 Q120 57 129 57 Q120 57 120 72 Q120 57 111 57 Q120 57 120 42 Z" fill="{star}"/></g>'''

for name, pfx, sun, line, star, txt in [
    ("logo-dark.svg", "d", "#FF8FB1", "#B8E1FF", "#F2A33A", "#E6EDF3"),
    ("logo-light.svg", "l", "#E0618C", "#2F6FD6", "#C97F08", "#1F2328"),
]:
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">'
           f'<title>Vesper</title>{mark(pfx, sun, line, star)}<path d="{word}" fill="{txt}"/></svg>\n')
    open(f"{out_dir}/{name}", "w").write(svg)
    print(name, W, H, len(svg))
