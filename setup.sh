#!/usr/bin/env bash
# One-time setup after cloning:
#   ./setup.sh /path/to/sifli-sdk-v2.5.1 [/path/to/upstream/SiFli-SDK]
#
# 1. MeshCore -> lib/MeshCore (git clone of the version in ci/pins.sh; MESHCORE_REF=<tag/branch>
#    to pick another)
# 2. the LilyGo SiFli SDK fixes this port needs (see sdk-patches/README.md):
#    quiet ADC driver patch, two missing Kconfig stubs, and rtos/env/packages (missing
#    from LilyGo's download; copied from the upstream OpenSiFli SDK if you pass its path)
# 3. ~/sifli-lilygo-sdk -> the SDK (env.sh uses it: the SDK breaks on paths with spaces)
set -e
cd "$(dirname "$0")"
sdk="${1:?usage: ./setup.sh /path/to/sifli-sdk-v2.5.1 [/path/to/upstream/SiFli-SDK]}"
upstream="$2"
sdk="$(cd "$sdk" && pwd -P)"
[ -f "$sdk/export.sh" ] || { echo "$sdk doesn't look like a SiFli SDK (no export.sh)"; exit 1; }

# 1. MeshCore
source ci/pins.sh
meshcore_ref="${MESHCORE_REF:-$MESHCORE_PIN}"
if [ ! -e lib/MeshCore ]; then
  echo "Fetching MeshCore ($meshcore_ref)"
  git clone --depth 1 --branch "$meshcore_ref" https://github.com/meshcore-dev/MeshCore lib/MeshCore
else
  echo "lib/MeshCore already present"
fi

# 2. SDK fixes
if patch -d "$sdk" -p1 -N --dry-run -s < sdk-patches/drv_adc-quiet.patch >/dev/null 2>&1; then
  patch -d "$sdk" -p1 -N -s < sdk-patches/drv_adc-quiet.patch && echo "SDK: ADC driver patched"
else
  echo "SDK: ADC driver patch already applied (or doesn't match; see sdk-patches/README.md)"
fi
(cd sdk-patches/stubs && find . -type f) | while read -r f; do
  [ -e "$sdk/$f" ] || { mkdir -p "$sdk/$(dirname "$f")"; cp "sdk-patches/stubs/$f" "$sdk/$f"; echo "SDK: added stub $f"; }
done
if [ ! -d "$sdk/rtos/env/packages" ]; then
  if [ -n "$upstream" ] && [ -d "$upstream/rtos/env/packages" ]; then
    cp -r "$upstream/rtos/env/packages" "$sdk/rtos/env/" && echo "SDK: copied rtos/env/packages from $upstream"
  else
    echo "SDK: rtos/env/packages is missing. Get the upstream SDK"
    echo "     (git clone https://github.com/OpenSiFli/SiFli-SDK) and rerun with its path as 2nd argument."
    exit 1
  fi
fi

# 3. SDK link
link="$HOME/sifli-lilygo-sdk"
if [ -L "$link" ] && [ "$(readlink -f "$link")" != "$sdk" ]; then
  echo "Note: $link points to $(readlink -f "$link"), not $sdk. Remove it to switch SDKs."
elif [ ! -e "$link" ]; then
  ln -s "$sdk" "$link" && echo "Linked $link -> $sdk"
fi
echo "Done. Next: ./build.sh companion && ./flash.sh companion"
