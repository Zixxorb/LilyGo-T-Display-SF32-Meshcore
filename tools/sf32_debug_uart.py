#!/usr/bin/env python3
"""Minimal client for the SF32LB52 hardware debug-over-UART channel.

This is the same framed protocol sftool uses (see OpenSiFli/sftool,
sftool-lib/src/sf32lb52/sifli_debug.rs). The chip's UART hardware recognises
it at whatever baud the UART is currently running.

Why this exists: sftool always says hello at 1 Mbaud. Our firmware runs the
console at 115200 (MeshCore's CLI baud), so sftool can't get in. `download`
enters debug mode at 115200, halts the CPU (otherwise MeshCore's CLI echoes
sftool's frames back and confuses it), stops the hardware watchdogs, and rewrites
UART1's baud register to 1 Mbaud.
The chip is then in the state sftool expects (console at 1 Mbaud), and the
normal sftool flow works. The firmware's own console baud is restored when the
newly flashed firmware boots.

usage:
  sf32_debug_uart.py PORT probe [BAUD]      enter + exit debug mode (harmless)
  sf32_debug_uart.py PORT download [BAUD]   switch the chip's UART to 1 Mbaud for sftool
  sf32_debug_uart.py PORT where [BAUD]      halt, print PC/LR/SP, resume (where is it stuck?)
  sf32_debug_uart.py PORT mem ADDR N [BAUD] read N words at ADDR (hex) while running
  sf32_debug_uart.py PORT shot ADDR OUT.png  save the 480x480 RGB565 frame buffer at ADDR
                                             (hex, RAM/PSRAM only) as a PNG; @ADDR = read
                                             the buffer address from the pointer at ADDR
                                             (SF32Display: @&sf32_shown_fb, see main.map)

Never read XIP flash (0x10000000-0x1FFFFFFF) over the debug channel: it stalls the bus
and the chip hangs until the watchdog reboots it. `mem` at a flash address reads the copy of
the flashed image instead (.flashed/main.bin, kept by flash.sh) and doesn't touch the board.
"""
import struct
import sys
import time

import serial

START = b"\x7e\x79"
ENTER = b"ATSF32\x05!"
EXIT = b"ATSF32\x18!"
UART1_BRR = 0x5008400C  # USART1_BASE 0x50084000 + BRR offset 0x0C
# The firmware runs the hardware watchdogs; stop them while the CPU is halted so
# they can't reset the chip mid-flash. WDT_CCR is at +0x0C, 0x34 = WDT_CMD_STOP.
WDT_STOP_REGS = (0x5009400C, 0x500CC00C)  # WDT1, IWDT
WDT_CMD_STOP = 0x34
DHCSR = 0xE000EDF0
DHCSR_HALT = 0xA05F0003  # DBGKEY | C_HALT | C_DEBUGEN
SFTOOL_BAUD = 1000000


def frame(payload: bytes) -> bytes:
    return START + struct.pack("<H", len(payload)) + b"\x10\x00" + payload


def recv(port: serial.Serial, timeout: float = 2.0) -> bytes:
    """Returns the payload of the next debug reply frame, or b'' on timeout.

    Frames that aren't replies are skipped: a running MeshCore CLI echoes every
    byte it receives, including our own command frames.
    """
    end = time.time() + timeout
    window = b""
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
            (n,) = struct.unpack("<H", hdr[:2])
            payload = b""
            while len(payload) < n and time.time() < end + 1:
                payload += port.read(n - len(payload))
            if payload[:1] in (b"\xd0", b"\xd1", b"\xd2", b"\xd3"):
                return payload
    return b""


def command(port: serial.Serial, payload: bytes) -> bytes:
    port.reset_input_buffer()
    port.write(frame(payload))
    port.flush()
    return recv(port)


def mem_write32(port, addr, value):
    return command(port, b"\x40\x77" + struct.pack("<IH", addr, 1) + struct.pack("<I", value))


def mem_read32(port, addr):
    r = command(port, b"\x40\x72" + struct.pack("<IH", addr, 1))
    return struct.unpack("<I", r[1:5])[0] if len(r) >= 5 and r[0] == 0xD2 else None


def mem_read_block(port, addr, nwords):
    r = command(port, b"\x40\x72" + struct.pack("<IH", addr, nwords))
    return r[1:1 + 4 * nwords] if len(r) >= 1 + 4 * nwords and r[0] == 0xD2 else None


def write_png(path, w, h, rgb):
    import zlib

    def chunk(tag, body):
        c = tag + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def screenshot(port, addr, out, w=480, h=480):
    total, chunk = w * h * 2, 4096
    data = bytearray()
    while len(data) < total:
        n = min(chunk, total - len(data))
        block = None
        for _ in range(3):
            block = mem_read_block(port, addr + len(data), n // 4)
            if block is not None:
                break
        if block is None:
            raise RuntimeError("read failed at offset %d" % len(data))
        data += block
    px = struct.unpack("<%dH" % (w * h), data)
    rgb = bytearray()
    for v in px:
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    write_png(out, w, h, rgb)


def enter(port) -> bool:
    """Debug channel in, CPU halted (both acknowledged), or False.

    A board with the power manager on (companion firmware) light-sleeps when its screen is
    off, and the debug channel doesn't answer then; it wakes every 25 ms for a few ms. So
    retry quickly (up to ~15 s) to catch an awake moment, and halt it in that same moment:
    only an acknowledged halt counts (if it dozed off in between, start over). Halted, it
    stays awake for whatever comes next; leave() resumes it.
    """
    halt = frame(b"\x40\x77" + struct.pack("<IH", DHCSR, 1) + struct.pack("<I", DHCSR_HALT))
    end = time.time() + 20
    entered = False
    entered_at = 0.0
    while time.time() < end:
        if not entered or time.time() - entered_at > 2:
            # a debug session left open (e.g. by an interrupted sftool) makes the chip answer
            # ENTER but drop every memory access: leave it first, then enter afresh
            port.write(frame(EXIT))
            port.flush()
            time.sleep(0.3)
            entered = False
            entered_at = time.time()
        if not entered:
            # the debug channel itself answers even while the chip sleeps
            port.reset_input_buffer()
            port.write(frame(ENTER))
            port.flush()
            entered = recv(port, timeout=0.03)[:1] == b"\xd1"
            if entered:
                entered_at = time.time()
            continue
        # a halt needs the chip's bus, only up during its brief awake moments: keep firing it
        # until one lands (acknowledged); halted, it stays awake for what comes next
        port.reset_input_buffer()
        port.write(halt)
        port.flush()
        if recv(port, timeout=0.02)[:1] == b"\xd3":
            return True
    return False


def leave(port):
    """Resume the CPU and leave the debug channel."""
    mem_write32(port, DHCSR, 0xA05F0001)  # DBGKEY | C_DEBUGEN: run
    command(port, EXIT)


FLASH_MAIN = 0x12020000  # where flash.sh writes main.bin (XIP address)


def mem_from_image(addr, n):
    """Flash addresses: read the flashed image on the PC (the same bytes), never the chip."""
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".flashed", "main.bin")
    try:
        data = open(path, "rb").read()
    except OSError:
        print("flash address: no .flashed/main.bin (flash once with flash.sh); not reading the chip")
        return 1
    print("(flash: from .flashed/main.bin, the board isn't touched)")
    for i in range(n):
        o = addr + 4 * i - FLASH_MAIN
        v = struct.unpack_from("<I", data, o)[0] if 0 <= o <= len(data) - 4 else None
        print("%08X: %s" % (addr + 4 * i, ("%08X" % v) if v is not None else "?"))
    return 0


def main():
    if len(sys.argv) < 3 or sys.argv[2] not in ("probe", "download", "where", "mem", "shot"):
        print(__doc__)
        return 2
    port_name, action = sys.argv[1], sys.argv[2]
    if action == "mem":
        mem_addr, mem_n = int(sys.argv[3], 16), int(sys.argv[4])
        baud = int(sys.argv[5]) if len(sys.argv) > 5 else 115200
        if 0x10000000 <= mem_addr < 0x20000000:
            return mem_from_image(mem_addr, mem_n)
    elif action == "shot":
        ptr = sys.argv[3].startswith("@")
        mem_addr, out, baud = int(sys.argv[3].lstrip("@"), 16), sys.argv[4], 115200
        if 0x10000000 <= mem_addr < 0x20000000:
            print("refusing to read XIP flash (hangs the chip)")
            return 1
    else:
        baud = int(sys.argv[3]) if len(sys.argv) > 3 else 115200
    with serial.Serial(port_name, baud, timeout=0.05) as port:
        port.rts = False
        if not enter(port):
            print(f"no response to debug-enter at {baud} baud")
            return 1
        # (halted by enter(); every path below resumes it (leave()) or hands it to sftool)
        if action == "mem":
            for i in range(mem_n):
                v = mem_read32(port, mem_addr + 4 * i)
                print("%08X: %s" % (mem_addr + 4 * i, ("%08X" % v) if v is not None else "?"))
            leave(port)
            return 0
        if action == "shot" and ptr:
            v = mem_read32(port, mem_addr)
            if v is None or not (0x20000000 <= v < 0x20080000 or 0x60000000 <= v < 0x60800000):
                print("bad frame buffer pointer", v)
                leave(port)
                return 1
            mem_addr = v
        if action == "shot":
            # Halt the CPU (a consistent frame), read at 1 Mbaud (about 5 s instead of 40),
            # then restore the console baud and resume.
            brr = mem_read32(port, UART1_BRR)
            if brr is None:
                print("could not read UART1 BRR")
                leave(port)
                return 1
            fast_brr = round(brr * baud / SFTOOL_BAUD)
            mem_write32(port, DHCSR, DHCSR_HALT)
            try:
                port.write(frame(b"\x40\x77" + struct.pack("<IH", UART1_BRR, 1) + struct.pack("<I", fast_brr)))
                port.flush()
                time.sleep(0.05)
                port.baudrate = SFTOOL_BAUD
                time.sleep(0.05)
                port.reset_input_buffer()
                screenshot(port, mem_addr, out)
            finally:
                port.write(frame(b"\x40\x77" + struct.pack("<IH", UART1_BRR, 1) + struct.pack("<I", brr)))
                port.flush()
                time.sleep(0.05)
                port.baudrate = baud
                time.sleep(0.05)
                port.reset_input_buffer()
                mem_write32(port, DHCSR, 0xA05F0001)  # resume
                command(port, EXIT)
            print("saved", out)
            return 0
        if action == "where":
            DCRSR, DCRDR = 0xE000EDF4, 0xE000EDF8
            mem_write32(port, DHCSR, DHCSR_HALT)
            time.sleep(0.05)
            regs = {}
            for name, idx in (("PC", 15), ("LR", 14), ("SP", 13), ("xPSR", 16), ("MSP", 17), ("PSP", 18)):
                mem_write32(port, DCRSR, idx)  # read request
                time.sleep(0.01)
                regs[name] = mem_read32(port, DCRDR)
            mem_write32(port, DHCSR, 0xA05F0001)  # DBGKEY | C_DEBUGEN: resume
            command(port, EXIT)
            print(" ".join("%s=%s" % (k, ("0x%08X" % v) if v is not None else "?") for k, v in regs.items()))
            return 0
        if action == "probe":
            mem_write32(port, DHCSR, 0xA05F0001)
            r = command(port, EXIT)
            print(f"debug channel OK at {baud} baud (exit reply: {r[:1].hex() or 'none'})")
            return 0
        # Halt first: while the firmware runs, MeshCore's CLI echoes our frames
        # (and answers "Unknown command") on the same TX line, which can garble replies.
        for _ in range(5):
            if mem_write32(port, DHCSR, DHCSR_HALT)[:1] == b"\xd3":
                break
            time.sleep(0.2)
        time.sleep(0.3)
        port.reset_input_buffer()
        for reg in WDT_STOP_REGS:
            mem_write32(port, reg, WDT_CMD_STOP)
        brr = None
        for _ in range(3):
            brr = mem_read32(port, UART1_BRR)
            if brr is not None:
                break
        if brr is None:
            print("could not read UART1 BRR")
            return 1
        # BRR = SystemFixClock / baud (bf0_hal_uart.c), same oversampling at both rates
        new_brr = round(brr * baud / SFTOOL_BAUD)
        if new_brr < 16:
            print(f"unexpected BRR {brr}; not changing it")
            return 1
        port.write(frame(b"\x40\x77" + struct.pack("<IH", UART1_BRR, 1) + struct.pack("<I", new_brr)))
        port.flush()
        time.sleep(0.1)  # the reply (if any) comes back at the new baud; not needed
    print(f"CPU halted, UART1 switched from {baud} to {SFTOOL_BAUD} baud (BRR {brr} -> {new_brr}); ready for sftool")
    return 0


if __name__ == "__main__":
    sys.exit(main())
