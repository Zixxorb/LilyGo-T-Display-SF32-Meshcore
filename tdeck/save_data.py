#!/usr/bin/env python3
"""Save the T-Deck's data partition (SPIFFS: identity, contacts, channels, settings) before a
flash.   save_data.py PORT OUT_DIR

Reading all 3.4 MB over the T-Deck's USB takes ~6 minutes, so it's saved at most once a day
(the user's choice, 2026-11-21: flashing the app never writes the data partition): if today's
copy exists, nothing is read. Otherwise the chip first works out the partition's MD5 itself (a
second or two): if it matches the newest copy, that copy is enough. FORCE_SAVE=1 always reads.
Exit 0 = saved (or not needed), 1 = not saved.
The copies hold the node's private key: board-logs/ is never committed.
"""
import glob
import hashlib
import os
import sys

sys.path.insert(0, os.path.expanduser("~/.platformio/packages/tool-esptoolpy"))
import esptool  # noqa: E402

DATA_OFFSET = 0xC90000  # spiffs in default_16MB.csv
DATA_SIZE = 0x360000


def newest_copy(logs):
    copies = sorted(glob.glob(os.path.join(logs, "*_tdeck*", "spiffs.bin")))
    return copies[-1] if copies else None


def main():
    port, out = sys.argv[1], sys.argv[2]
    logs = os.path.dirname(os.path.abspath(out))
    today = os.path.basename(out)[:10]  # YYYY-MM-DD
    last = newest_copy(logs)
    if last and not os.environ.get("FORCE_SAVE") and os.path.basename(os.path.dirname(last)).startswith(today):
        print(f"Data partition already saved today ({os.path.relpath(last)}): not read again")
        return 0
    esp = esptool.cmds.detect_chip(port, 115200)
    esp = esp.run_stub()
    try:
        esp.change_baud(921600)
        on_chip = esp.flash_md5sum(DATA_OFFSET, DATA_SIZE)
        if last and hashlib.md5(open(last, "rb").read()).hexdigest() == on_chip:
            print(f"Data partition unchanged since {os.path.relpath(last)}: not read again")
            return 0
        os.makedirs(out, exist_ok=True)
        print(f"Saving the data partition to {out} (about 6 minutes)")
        data = esp.read_flash(DATA_OFFSET, DATA_SIZE)
        if hashlib.md5(data).hexdigest() != on_chip:
            print("The copy doesn't match the chip's checksum")
            return 1
        with open(os.path.join(out, "spiffs.bin"), "wb") as f:
            f.write(data)
        with open(os.path.join(out, "spiffs.bin.md5"), "w") as f:
            f.write(on_chip + "  spiffs.bin\n")
        return 0
    finally:
        esp.hard_reset()
        esp._port.close()


if __name__ == "__main__":
    sys.exit(main())
