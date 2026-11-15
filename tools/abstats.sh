#!/bin/bash
# Battery A/B test (SF32_LINK_AB=1: low power mode's slow Bluetooth link rhythm, 45 min stretches):
# after a night on battery, plug USB back in and run this. From the unplug, normal and test
# stretches alternate; it reads the battery voltage logged every 15 min (sf32_ab_log,
# SF32Board.cpp) and compares each test stretch with the normal ones either side. The ELF
# must be the flashed build.
cd "$(dirname "$0")/.."
source ./env.sh >/dev/null 2>&1
elf="${ELF:-project_companion/build_t-display-sf32_hcpu/main.elf}"
addr=$(arm-none-eabi-nm "$elf" | awk '/ sf32_ab_log$/{print $1}')
[ -n "$addr" ] || { echo "no sf32_ab_log in this build (build with SF32_LINK_AB=1)"; exit 1; }
export AB_A="link normal" AB_B="link SLOW" AB_STRETCH=3
port=$(readlink -f $(ls /dev/serial/by-id/*1a86* 2>/dev/null | head -1) 2>/dev/null)
[ -n "$port" ] || { echo "board not found"; exit 1; }
python3 tools/sf32_debug_uart.py "$port" mem "$addr" 38 | python3 -I -c '
import sys, os
w = [int(l.split()[1], 16) for l in sys.stdin if ":" in l]
if len(w) < 38 or w[0] != 0x53464142: sys.exit("no test log yet (has it run on battery?)")
n = min(w[1], 64)
A, B = os.environ["AB_A"], os.environ["AB_B"]
S = int(os.environ["AB_STRETCH"])
mv = [x for v in w[2:34] for x in (v & 0xFFFF, v >> 16)][:n]
# the companion'"'"'s voltage -> charge table (ui_theme.inc), for % per hour
curve = [(4180, 100), (4100, 90), (4000, 79), (3900, 66), (3800, 52), (3750, 42), (3700, 31),
         (3650, 21), (3600, 13), (3500, 6), (3300, 0)]
def pct(v):
    if v >= curve[0][0]: return 100.0
    for (v1, p1), (v0, p0) in zip(curve, curve[1:]):
        if v >= v0: return p0 + (v - v0) * (p1 - p0) / (v1 - v0)
    return 0.0
print(f"{n} samples ({(n - 1) * 15} min on battery)")
print(f"phone connected: {A} {w[34] / 60000:.0f} min, {B} {w[36] / 60000:.0f} min (the comparison only holds while connected)")
print("  ".join(f"{i * 15 // 60}:{i * 15 % 60:02d} {v}" for i, v in enumerate(mv)))
ph = []  # (stretch, mV/h, %/h)
for k in range(0, (n - 1) // S + 1):
    a, b = S * k, min(S * k + S, n - 1)
    if k == 0: a = 2  # the first half hour: the voltage still settling after the unplug
    if b - a < S // 2 + 1: continue  # too short to mean anything
    h = (b - a) / 4
    ph.append((k, (mv[a] - mv[b]) / h, (pct(mv[a]) - pct(mv[b])) / h))
    kind = f"{B if k % 2 else A:12s}"
    print(f"{a * 15 / 60:4.1f}-{b * 15 / 60:4.1f} h  {kind}  {mv[a]}->{mv[b]} mV  {ph[-1][1]:5.1f} mV/h")
# each test stretch against the normal stretches either side (the mV per % changes
# slowly with the charge, so neighbours compare fairly)
by = {k: r for k, r, _ in ph}
ratios = []
for k in by:
    if k % 2 and (k - 1 in by or k + 1 in by):
        near = [by[j] for j in (k - 1, k + 1) if j in by]
        ratios.append(by[k] / (sum(near) / len(near)))
if not ratios: sys.exit("not enough of the test ran yet (needs a test stretch)")
r = sum(ratios) / len(ratios)
# the total, from the charge table where it is not flat at 100 % (below 4.18 V)
lo = [i for i in range(2, n) if mv[i] < 4170]
total = None
if len(lo) >= 9:
    hrs = (lo[-1] - lo[0]) / 4
    total = (pct(mv[lo[0]]) - pct(mv[lo[-1]])) / hrs * 4.8  # mA, 480 mAh
on_ma = total * 2 / (1 + r) if total else None  # total: the average of on and off stretches
print(f"{B} drains {100 * r:.0f} % as fast as {A}: that is ~{100 * (1 - r):.0f} % of the drain"
      + (f", ~{(1 - r) * on_ma:.1f} mA of ~{on_ma:.1f} mA (if the battery is 480 mAh)" if on_ma else ""))
'
