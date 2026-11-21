# Sourced by flash.sh and monitor.sh: the T-Deck's serial port, by its USB id (Espressif's own
# USB, so the SF32 board's CH343 port is never picked). Pass a port to use another.
tdeck_port() {
  if [ -n "$1" ]; then echo "$1"; return; fi
  local ids=(/dev/serial/by-id/usb-Espressif*)
  [ -e "${ids[0]}" ] || return 1
  [ ${#ids[@]} -eq 1 ] || { echo "More than one Espressif board plugged in; pass the port" >&2; return 1; }
  readlink -f "${ids[0]}"
}
