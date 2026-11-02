# Source this from bash or zsh (not fish) to activate the LilyGo SiFli SDK:
#   source env.sh
#
# The SDK: $SF32_SDK_DIR, or ../SlFli-SDK-Lilygo-master/sifli-sdk-v2.5.1 next to this
# folder. Its linker flags break on paths containing spaces, so we activate it through a
# space-free symlink (~/sifli-lilygo-sdk) and point SIFLI_SDK at that.
SF32_SDK_LINK="$HOME/sifli-lilygo-sdk"
_here="$(cd "$(dirname "${BASH_SOURCE[0]:-${(%):-%x}}")" && pwd -P)"
if [ ! -e "$SF32_SDK_LINK" ]; then
  _sdk="${SF32_SDK_DIR:-$_here/../SlFli-SDK-Lilygo-master/sifli-sdk-v2.5.1}"
  if [ ! -d "$_sdk" ]; then
    echo "SiFli SDK not found at $_sdk: set SF32_SDK_DIR (see README)" >&2
    unset _here _sdk
    return 1 2>/dev/null || exit 1
  fi
  ln -s "$(cd "$_sdk" && pwd -P)" "$SF32_SDK_LINK"
fi
. "$SF32_SDK_LINK/export.sh" --profile default >/dev/null
export SIFLI_SDK="$SF32_SDK_LINK" SIFLI_SDK_PATH="$SF32_SDK_LINK"
export PYTHONPATH="$SF32_SDK_LINK/tools/build"
unset _here _sdk
