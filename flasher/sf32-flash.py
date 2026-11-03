#!/usr/bin/env python3
"""Flash MeshCore onto a LilyGo T-Display-SF32: no SDK or compiler needed.

    python3 sf32-flash.py                        # asks which firmware, downloads it, flashes
    python3 sf32-flash.py companion              # or repeater / room_server
    python3 sf32-flash.py path/to/companion.zip  # a firmware zip you downloaded (or a folder)
    python3 sf32-flash.py companion --port COM5  # pick the port yourself

Needs Python 3.8+ and pyserial (`pip install pyserial`). The first run downloads SiFli's
flashing tool, sftool, for your system (checked against a pinned SHA-256) and keeps it in
a cache folder.

Why not plain sftool: it always connects at 1 Mbaud, but this firmware's console runs at
115200 (MeshCore's CLI baud). So first the chip's hardware debug channel (which answers at
the console's current baud) is used to halt the CPU, stop its watchdogs and switch the
UART to 1 Mbaud; then sftool works as usual. (Same as tools/sf32_debug_uart.py download.)
"""
import argparse
import hashlib
import io
import os
import platform
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile

REPO = "Zixxorb/LilyGo-T-Display-SF32-Meshcore"
FIRMWARES = {
    "companion": "Companion: pairs with the MeshCore phone app over Bluetooth, touch screen UI",
    "repeater": "Repeater: extends the mesh (configure at config.meshcore.io)",
    "room_server": "Room server: a shared message board on the mesh",
}
# flash layout (the SDK's ftab for this board)
IMAGES = (("bootloader.bin", 0x12010000), ("main.bin", 0x12020000), ("ftab.bin", 0x12000000))

SFTOOL_VERSION = "0.2.5"
SFTOOL_URL = "https://github.com/OpenSiFli/sftool/releases/download/{v}/{name}"
SFTOOL_ASSETS = {  # (system, machine) -> (file, sha256)
    ("Linux", "x86_64"): ("sftool-0.2.5-x86_64-unknown-linux-gnu.tar.xz",
                          "ab12738de1be0277f0304a217a3773bea9495d088b438839c7739150ee37a905"),
    ("Linux", "aarch64"): ("sftool-0.2.5-aarch64-unknown-linux-gnu.tar.xz",
                           "135eeaf23e50d5c88fd60edbbda34e11fb8311e0ce702c2f585aba6f88355987"),
    ("Linux", "i686"): ("sftool-0.2.5-i686-unknown-linux-gnu.tar.xz",
                        "406d1afcf9d541481b29fff04ec89f4b5210e35d9850a588152266327a8705d7"),
    ("Darwin", "x86_64"): ("sftool-0.2.5-x86_64-apple-darwin.tar.xz",
                           "80fdb4e5c54e31b13bdd47f09a3a8951f2dbd18316d5577c8321059608ca373b"),
    ("Darwin", "arm64"): ("sftool-0.2.5-aarch64-apple-darwin.tar.xz",
                          "87d968052ebf84ecc535fbbe60b2861d43d7aabc376e8baee0687be137373c5e"),
    ("Windows", "AMD64"): ("sftool-0.2.5-x86_64-pc-windows-msvc.zip",
                           "64a8c52901553858a4efb9f9484cde7e71473b5b70a7803956fd22a6dcdebb5b"),
    ("Windows", "x86"): ("sftool-0.2.5-i686-pc-windows-msvc.zip",
                         "bf7b442e70aa4b852b0fc0c927724ef1181a502ea62d1e647a0364150008ad9f"),
}
USB_VID = 0x1A86  # the board's CH343P USB-serial chip (WCH)

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("This needs pyserial. Install it with:\n    python3 -m pip install pyserial\n"
             "(on Windows: py -m pip install pyserial), then run this again.")


def fail(msg):
    sys.exit("\nError: " + msg)


# ---------------------------------------------------------------- firmware

def ask_firmware():
    names = list(FIRMWARES)
    print("Which firmware?")
    for i, n in enumerate(names, 1):
        print(f"  {i}. {FIRMWARES[n]}")
    while True:
        a = input(f"Choose 1-{len(names)}: ").strip()
        if a.isdigit() and 1 <= int(a) <= len(names):
            return names[int(a) - 1]
        if a in names:
            return a


def download(url):
    req = urllib.request.Request(url, headers={"User-Agent": "sf32-flash"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def load_firmware(spec, workdir):
    """-> folder holding ftab.bin, bootloader.bin, main.bin"""
    if os.path.isdir(spec):
        folder = spec
    elif os.path.isfile(spec):
        with zipfile.ZipFile(spec) as z:
            z.extractall(workdir)
        folder = workdir
    else:  # a firmware name: the latest release on GitHub
        url = f"https://github.com/{REPO}/releases/latest/download/{spec}.zip"
        print(f"Downloading {spec} firmware from {url}")
        try:
            data = download(url)
        except Exception as e:
            fail(f"couldn't download it ({e}).\nDownload {spec}.zip from "
                 f"https://github.com/{REPO}/releases yourself and pass its path instead.")
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            z.extractall(workdir)
        folder = workdir
    # the files may be in a subfolder (companion/...)
    for root, _, files in os.walk(folder):
        if all(name in files for name, _ in IMAGES):
            return root
    fail(f"no {', '.join(n for n, _ in IMAGES)} found in {spec}")


# ---------------------------------------------------------------- sftool

def cache_dir():
    if platform.system() == "Windows":
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
    elif platform.system() == "Darwin":
        base = os.path.expanduser("~/Library/Caches")
    else:
        base = os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache")
    return os.path.join(base, "sf32-flash")


def find_sftool(given):
    if given:
        return given
    exe = "sftool.exe" if platform.system() == "Windows" else "sftool"
    target = os.path.join(cache_dir(), f"sftool-{SFTOOL_VERSION}", exe)
    if os.path.isfile(target):
        return target
    key = (platform.system(), platform.machine())
    if key not in SFTOOL_ASSETS:
        on_path = shutil.which("sftool")
        if on_path:
            return on_path
        fail(f"no sftool download for {key[0]} {key[1]}. Install sftool "
             f"(https://github.com/OpenSiFli/sftool) and pass --sftool PATH.")
    name, sha = SFTOOL_ASSETS[key]
    print(f"Downloading sftool {SFTOOL_VERSION} (SiFli's flashing tool, first run only)")
    data = download(SFTOOL_URL.format(v=SFTOOL_VERSION, name=name))
    if hashlib.sha256(data).hexdigest() != sha:
        fail("the sftool download doesn't match its expected checksum; not using it")
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if name.endswith(".zip"):
        arc = zipfile.ZipFile(io.BytesIO(data))
        members = [(m, lambda m=m: arc.read(m)) for m in arc.namelist()]
    else:
        arc = tarfile.open(fileobj=io.BytesIO(data), mode="r:xz")
        members = [(m.name, lambda m=m: arc.extractfile(m).read()) for m in arc.getmembers() if m.isfile()]
    for mname, read in members:
        if os.path.basename(mname) == exe:
            with open(target, "wb") as f:
                f.write(read())
            os.chmod(target, 0o755)
            return target
    fail("sftool wasn't in the download")


# ---------------------------------------------------------------- port

def find_port(given):
    if given:
        return given
    ports = [p for p in serial.tools.list_ports.comports() if p.vid == USB_VID]
    if not ports:
        fail("no T-Display-SF32 found. Plug it in with a data USB cable (and on Windows, check "
             "Device Manager for a COM port; the CH343 driver may be needed), or pass --port.")
    if len(ports) == 1:
        return ports[0].device
    print("More than one possible board:")
    for i, p in enumerate(ports, 1):
        print(f"  {i}. {p.device}  {p.description}")
    while True:
        a = input(f"Choose 1-{len(ports)}: ").strip()
        if a.isdigit() and 1 <= int(a) <= len(ports):
            return ports[int(a) - 1].device


# ---------------------------------------------------------------- debug channel

START, ENTER, EXIT = b"\x7e\x79", b"ATSF32\x05!", b"ATSF32\x18!"
UART1_BRR = 0x5008400C
WDT_STOP_REGS = (0x5009400C, 0x500CC00C)  # WDT1, IWDT: WDT_CCR, 0x34 = stop
DHCSR, DHCSR_HALT = 0xE000EDF0, 0xA05F0003
SFTOOL_BAUD = 1000000


def frame(payload):
    return START + struct.pack("<H", len(payload)) + b"\x10\x00" + payload


def recv(port, timeout=2.0):
    end, window = time.time() + timeout, b""
    while time.time() < end:
        b = port.read(1)
        if not b:
            continue
        window = (window + b)[-2:]
        if window == START:
            window = b""
            hdr = port.read(4)
            if len(hdr) < 4:
                continue
            n = struct.unpack("<H", hdr[:2])[0]
            payload = b""
            while len(payload) < n and time.time() < end + 1:
                payload += port.read(n - len(payload))
            if payload[:1] in (b"\xd0", b"\xd1", b"\xd2", b"\xd3"):
                return payload
    return b""


def command(port, payload, timeout=2.0):
    port.reset_input_buffer()
    port.write(frame(payload))
    port.flush()
    return recv(port, timeout)


def write32(port, addr, value, timeout=2.0):
    return command(port, b"\x40\x77" + struct.pack("<IH", addr, 1) + struct.pack("<I", value), timeout)


def read32(port, addr):
    r = command(port, b"\x40\x72" + struct.pack("<IH", addr, 1))
    return struct.unpack("<I", r[1:5])[0] if len(r) >= 5 and r[0] == 0xD2 else None


def prepare(port_name, baud):
    """Halt the chip and switch its UART to 1 Mbaud for sftool. True if done."""
    with serial.Serial(port_name, baud, timeout=0.05) as port:
        port.rts = False
        # The companion sleeps between tasks with the screen off (on battery); its debug
        # channel answers "enter" even then, but a halt only lands while it's awake. Keep
        # trying for a while to catch an awake moment.
        halt = frame(b"\x40\x77" + struct.pack("<IH", DHCSR, 1) + struct.pack("<I", DHCSR_HALT))
        start = time.time()
        end, entered, entered_at, ever = start + 15, False, 0.0, False
        while time.time() < end:
            # this firmware answers "enter" within a moment even asleep: no answer at all
            # for 3 s means another baud (or firmware), so don't wait the full 15 s
            if not ever and time.time() - start > 3:
                return False
            if not entered or time.time() - entered_at > 2:
                port.write(frame(EXIT))  # a debug session left open drops memory access: leave it
                port.flush()
                time.sleep(0.3)
                port.reset_input_buffer()
                port.write(frame(ENTER))
                port.flush()
                entered = recv(port, 0.05)[:1] == b"\xd1"
                ever = ever or entered
                entered_at = time.time()
                continue
            port.reset_input_buffer()
            port.write(halt)
            port.flush()
            if recv(port, 0.02)[:1] == b"\xd3":
                break
        else:
            return False
        time.sleep(0.3)
        port.reset_input_buffer()
        for reg in WDT_STOP_REGS:
            write32(port, reg, 0x34)
        brr = None
        for _ in range(3):
            brr = read32(port, UART1_BRR)
            if brr is not None:
                break
        if brr is None:
            return False
        new_brr = round(brr * baud / SFTOOL_BAUD)
        if new_brr < 16:
            return False
        port.write(frame(b"\x40\x77" + struct.pack("<IH", UART1_BRR, 1) + struct.pack("<I", new_brr)))
        port.flush()
        time.sleep(0.1)
    return True


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="Flash MeshCore onto a LilyGo T-Display-SF32.")
    ap.add_argument("firmware", nargs="?", help="companion, repeater, room_server, or a firmware zip / folder")
    ap.add_argument("--port", help="serial port (default: found automatically)")
    ap.add_argument("--sftool", help="path to sftool (default: downloaded automatically)")
    args = ap.parse_args()

    spec = args.firmware or ask_firmware()
    with tempfile.TemporaryDirectory() as tmp:
        folder = load_firmware(spec, tmp)
        sftool = find_sftool(args.sftool)
        port = find_port(args.port)
        print(f"\nFlashing {spec} to {port}. Don't unplug the board until it's done.")
        print("Your settings, contacts and node identity are kept.\n")

        # a board running this firmware: 115200; something else (factory firmware, a crash): 1M
        if not (prepare(port, 115200) or prepare(port, SFTOOL_BAUD)):
            print("(The board's debug channel didn't answer; trying sftool directly.)")
        cmd = [sftool, "-p", port, "-c", "SF32LB52", "-m", "nor", "write_flash"]
        cmd += [f"{os.path.join(folder, name)}@0x{addr:08X}" for name, addr in IMAGES]
        r = subprocess.run(cmd)
    if r.returncode != 0:
        fail("flashing failed (see above). Unplug the board, plug it back in, and try again.")
    print("\nDone. The board restarts with the new firmware.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit("\nStopped.")
