#!/bin/bash
# Screenshot the panel: tools/shot.sh OUT.png [firmware]  (firmware: companion by default)
# Reads the frame currently shown (SF32Display's sf32_shown_fb) via the debug channel.
cd "$(dirname "$0")/.."
fw=${2:-companion}
case $fw in companion) proj=project_companion;; room_server) proj=project_room_server;; *) proj=project;; esac
source ./env.sh >/dev/null 2>&1
addr=$(arm-none-eabi-nm $proj/build_t-display-sf32_hcpu/main.elf | awk '/ sf32_shown_fb$/{print $1}')
port=$(readlink -f $(ls /dev/serial/by-id/*1a86* 2>/dev/null | head -1) 2>/dev/null)  # the board's CH343 (not another device on ttyACM*)
exec python3 tools/sf32_debug_uart.py "$port" shot "@$addr" "$1"
