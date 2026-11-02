#!/usr/bin/env bash
# Build the firmware.
#   ./build.sh [repeater|room_server|companion|selftest] [extra scons args, e.g. -c to clean]
# repeater (default): MeshCore simple_repeater          -> project/
# room_server:        MeshCore simple_room_server        -> project_room_server/
# companion:          MeshCore companion_radio (BLE)     -> project_companion/
# selftest:           bring-up self-tests                -> project/
set -e
cd "$(dirname "$0")"
target="${1:-repeater}"
case "$target" in
  repeater|room_server|companion|selftest) shift || true ;;
  *) target=repeater ;;
esac
source ./env.sh
case "$target" in
  companion) export SF32_FIRMWARE=companion; dir=project_companion ;;
  room_server) export SF32_FIRMWARE=room_server; dir=project_room_server ;;
  selftest)  export SF32_FIRMWARE=repeater SF32_SELFTEST=1; dir=project ;;
  *)         export SF32_FIRMWARE=repeater; dir=project ;;
esac
# SCons tracks #include lines in sources, but not force-included headers
# (-include sf32_platform.h / variant.h / sf32_companion.h, and what they include) or
# the -I compat path. When any header under src/ changes, drop our and MeshCore's
# objects (not the SDK's) so nothing is left compiled against an old definition.
stamp="$dir/build_t-display-sf32_hcpu/.headers.sha256"
hash="$( { echo "$target"; find src -name '*.h' -print0 | sort -z | xargs -0 sha256sum; } | sha256sum | cut -d' ' -f1)"
if [ -f "$stamp" ] && [ "$(cat "$stamp")" != "$hash" ]; then
  echo "Headers changed: rebuilding app/library objects"
  rm -rf "$dir/build_t-display-sf32_hcpu/lib" "$dir/build_t-display-sf32_hcpu/src"
fi

cd "$dir"
scons --board=t-display-sf32 -j"$(nproc)" "$@"
mkdir -p build_t-display-sf32_hcpu && echo "$hash" > build_t-display-sf32_hcpu/.headers.sha256
