#!/usr/bin/env bash
# Build Vesper for the T-Deck.   ./build.sh   (PlatformIO: ~/.platformio, or pio on PATH)
set -e
cd "$(dirname "$0")"
pio=$(command -v pio || echo ~/.platformio/penv/bin/pio)
[ -d ../lib/MeshCore/src ] || { echo "MeshCore not found: run ../setup.sh first"; exit 1; }
"$pio" run "$@"
