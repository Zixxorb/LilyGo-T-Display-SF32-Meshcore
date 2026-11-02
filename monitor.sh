#!/usr/bin/env bash
# Serial console (115200 baud). Exit with Ctrl+].   ./monitor.sh [/dev/ttyACM0]
set -e
cd "$(dirname "$0")"
source ./env.sh
port="${1:-$(readlink -f $(ls /dev/serial/by-id/*1a86* 2>/dev/null | head -n1) 2>/dev/null)}"
[ -n "$port" ] || { echo "No serial port found; pass one, e.g. ./monitor.sh /dev/ttyACM0"; exit 1; }
exec python -m serial.tools.miniterm --eol CR "$port" 115200
