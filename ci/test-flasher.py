#!/usr/bin/env python3
"""Check flasher/sf32-flash.py without a board: .github/workflows/flasher.yml runs this on
Windows, macOS and Linux. It covers downloading and running sftool, reading firmware zips,
finding the port and the error messages. It can't cover writing to a board.

Safe with a board plugged in: the checks that look for a real board only run on CI, with
none attached, and the others use a port name that can't exist.

    python ci/test-flasher.py                       # downloads sftool
    SF32_TEST_SFTOOL=/path/to/sftool python ci/test-flasher.py   # uses that one instead
"""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLASHER = os.path.join(ROOT, "flasher", "sf32-flash.py")
NO_PORT = "NO_SUCH_PORT"

tmp = tempfile.mkdtemp(prefix="sf32-flash-test")
# keep sftool's download out of the real cache folder (cache_dir() in the flasher)
env_cache = os.path.join(tmp, "cache")
os.environ.update(XDG_CACHE_HOME=env_cache, LOCALAPPDATA=env_cache, HOME=env_cache)

spec = importlib.util.spec_from_file_location("sf32_flash", FLASHER)
flasher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flasher)
list_ports = flasher.serial.tools.list_ports

failures = []


def check(name, ok, detail=""):
    print(("ok    " if ok else "FAIL  ") + name)
    if not ok:
        failures.append(name)
        if detail:
            print("      " + str(detail).replace("\n", "\n      "))


def cli(*args):
    """Run the flasher as a user would -> (exit code, output)"""
    r = subprocess.run([sys.executable, FLASHER] + list(args), stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    return r.returncode, r.stdout.decode("utf-8", "replace")


def check_error(name, expected, code, out):
    """It stopped with an error message for a person, not a Python traceback."""
    check(name, code != 0 and expected in out and "Traceback" not in out, out)


def error_of(func, *args):
    """The message a fail() in the flasher gives, or None if it didn't stop."""
    try:
        func(*args)
    except SystemExit as e:
        return str(e)
    return None


def make_zip(path, names):
    with zipfile.ZipFile(path, "w") as z:
        for n in names:
            z.writestr(n, "x")


IMAGES = [n for n, _ in flasher.IMAGES]
good = os.path.join(tmp, "companion.zip")
make_zip(good, ["companion/" + n for n in IMAGES] + ["companion/main.elf", "companion/LICENSE",
                                                     "companion/licenses/OFL-NotoSans.txt"])

code, out = cli("--help")
check("--help", code == 0 and "firmware" in out, out)

# firmware
work = os.path.join(tmp, "unpacked")
os.makedirs(work)
found = flasher.load_firmware(good, work)
check("a release-style zip: finds the images", all(os.path.isfile(os.path.join(found, n)) for n in IMAGES), found)
check("a folder works too", flasher.load_firmware(found, os.path.join(tmp, "unused")) == found)

bad = os.path.join(tmp, "bad.zip")
with open(bad, "wb") as f:
    f.write(b"this is not a zip")
check_error("a damaged zip", "valid zip", *cli(bad))
partial = os.path.join(tmp, "partial.zip")
make_zip(partial, ["companion/main.bin"])
check_error("a zip with images missing", "found in", *cli(partial))

# sftool download: failures first, with nothing in the cache yet
key = (flasher.platform.system(), flasher.platform.machine())
check("this system has an sftool download", key in flasher.SFTOOL_ASSETS, key)
real_download = flasher.download


def no_network(url):
    raise OSError("no network")


flasher.download = no_network
msg = error_of(flasher.find_sftool, None)
check("sftool download fails: a clear message", msg and "couldn't download sftool" in msg, msg)
flasher.download = lambda url: b"not the real file"
msg = error_of(flasher.find_sftool, None)
check("sftool checksum wrong: refused", msg and "checksum" in msg, msg)
flasher.download = real_download

sftool = os.environ.get("SF32_TEST_SFTOOL")
if not sftool:
    sftool = flasher.find_sftool(None)  # the real download, checked against the pinned SHA-256
    print("      sftool: " + sftool)
r = subprocess.run([sftool, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
check("sftool runs here", r.returncode == 0 and flasher.SFTOOL_VERSION.encode() in r.stdout, r.stdout)

# the command the flasher builds: sftool must accept its form on this system (drive letters
# in Windows paths), then stop at the port, which doesn't exist
cmd = [sftool, "-p", NO_PORT, "-c", "SF32LB52", "-m", "nor", "write_flash"]
cmd += [f"{os.path.join(found, n)}@0x{a:08X}" for n, a in flasher.IMAGES]
r = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
text = r.stdout.decode("utf-8", "replace")
check("sftool accepts the command line", r.returncode != 0 and "Usage" not in text and "invalid value" not in text, text)

# ports (no real port is opened: the list is faked, or the name can't exist)
real_comports = list_ports.comports
list_ports.comports = lambda: []
msg = error_of(flasher.find_port, None)
check("no board found: a clear message", msg and "no T-Display-SF32 found" in msg, msg)


class FakePort:
    vid, device, description = flasher.USB_VID, "FAKE", "fake"


list_ports.comports = lambda: [FakePort()]
check("one board: picked by itself", flasher.find_port(None) == "FAKE")
list_ports.comports = real_comports
check_error("a port that doesn't exist", "couldn't open", *cli(good, "--port", NO_PORT, "--sftool", sftool))

# the whole way through. CI only, and never with a board attached: this would flash it.
board = [p.device for p in real_comports() if p.vid == flasher.USB_VID]
if os.environ.get("CI") and not board:
    check_error("end to end, no board", "no T-Display-SF32 found", *cli(good, "--sftool", sftool))
    code, out = cli("companion", "--sftool", sftool)  # no release to download yet, or no board
    check("by firmware name: stops cleanly", code != 0 and "Traceback" not in out and "Error:" in out, out)
else:
    print("skip  end to end (CI only, with no board attached)")

shutil.rmtree(tmp, ignore_errors=True)
if failures:
    sys.exit(f"\n{len(failures)} failed: " + ", ".join(failures))
print("\nAll passed.")
