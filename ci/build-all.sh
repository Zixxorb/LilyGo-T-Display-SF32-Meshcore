#!/usr/bin/env bash
# Build all three firmwares on a clean machine: GitHub Actions runs this (see
# .github/workflows/build.yml), and it works the same locally.
#
#   ci/build-all.sh            -> dist/<firmware>.zip (bootloader.bin, main.bin, ftab.bin, main.elf)
#
# It fetches pinned versions of what the port builds on into $SF32_CI_DIR (default .ci/):
# LilyGo's SiFli SDK (only its sifli-sdk-v2.5.1 folder), the upstream SiFli SDK (only
# rtos/env/packages, which LilyGo's lacks) and MeshCore, installs the SDK's toolchain (needs
# uv) and runs ./setup.sh and ./build.sh. Change the pins below to move to newer versions.
set -euo pipefail
cd "$(dirname "$0")/.."
root="$(pwd -P)"

LILYGO_SDK_REPO=https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo
LILYGO_SDK_SHA=036b3649688477df9523f9c7884e77f883feed8f
UPSTREAM_SDK_REPO=https://github.com/OpenSiFli/SiFli-SDK
UPSTREAM_SDK_SHA=e203f6e0cc222204c6af236b9b2d0f33d16f0dc3
export MESHCORE_REF="${MESHCORE_REF:-companion-v1.18.0}"

work="${SF32_CI_DIR:-$root/.ci}"
mkdir -p "$work"
work="$(cd "$work" && pwd -P)"

# fetch: a sparse, shallow checkout of one folder of a repo at a pinned commit
fetch() {  # repo sha dir path
  local repo=$1 sha=$2 dir=$3 path=$4
  if [ -f "$dir/.fetched" ] && [ "$(cat "$dir/.fetched")" = "$sha" ]; then
    echo "$dir: already at $sha"
    return
  fi
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" remote add origin "$repo"
  git -C "$dir" sparse-checkout set "$path"
  git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$sha"
  git -C "$dir" checkout -q FETCH_HEAD
  echo "$sha" > "$dir/.fetched"
}

echo "== SDKs"
fetch "$LILYGO_SDK_REPO" "$LILYGO_SDK_SHA" "$work/lilygo-sdk" sifli-sdk-v2.5.1
fetch "$UPSTREAM_SDK_REPO" "$UPSTREAM_SDK_SHA" "$work/upstream-sdk" rtos/env/packages
sdk="$work/lilygo-sdk/sifli-sdk-v2.5.1"

echo "== Toolchain"
(cd "$sdk" && bash ./install.sh)

echo "== Setup (MeshCore $MESHCORE_REF, SDK fixes)"
./setup.sh "$sdk" "$work/upstream-sdk"

echo "== Build"
rm -rf dist
mkdir -p dist
for fw in companion repeater room_server; do
  ./build.sh "$fw"
  case "$fw" in
    companion) dir=project_companion ;;
    room_server) dir=project_room_server ;;
    *) dir=project ;;
  esac
  out="$dir/build_t-display-sf32_hcpu"
  stage="dist/$fw"
  mkdir -p "$stage"
  cp "$out/bootloader/bootloader.bin" "$out/main.bin" "$out/ftab/ftab.bin" "$out/main.elf" "$stage/"
  (cd dist && python3 -m zipfile -c "$fw.zip" "$fw" && rm -rf "$fw")
  echo "dist/$fw.zip"
done
cp flasher/sf32-flash.py dist/
