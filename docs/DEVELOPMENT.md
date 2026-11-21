# Development notes

How the port works inside, how it's built, and how to debug it. For using the firmware,
see the [README](../README.md).

- [Project layout](#project-layout)
- [How the build fits together](#how-the-build-fits-together)
- [Fixes the LilyGo SDK needs](#fixes-the-lilygo-sdk-needs)
- [Flashing and debugging](#flashing-and-debugging)
- [Screenshots and test builds](#screenshots-and-test-builds)
- [Display](#display)
- [Power and sleep](#power-and-sleep)
- [Sound, sensors, diagnostics](#sound-sensors-diagnostics)
- [Bluetooth and USB](#bluetooth-and-usb)
- [Radio](#radio)
- [Other notes](#other-notes)
- [Measurements](#measurements)

## Project layout

```
project*/         SiFli/SCons projects, one per firmware (proj.conf = Kconfig options;
                  project_companion adds Bluetooth)
src/main.cpp      runs MeshCore's setup()/loop() as the RT-Thread main thread
src/platform/     T-Display-SF32 specifics: SF32Board (battery, charger, watchdog,
                  power-off, sleep, CLI extras), SF32SX1262Radio (mesh::Radio), SF32Display
                  (AMOLED), SF32TouchInput, SF32SerialBLEInterface (+_events.c),
                  SF32UsbInterface, SF32RTCClock, SF32Motion, SF32Sound, SF32HardwareRNG,
                  target.h/.cpp, variant.h (defaults), compat/ (companion-only header shims)
src/arduino/      minimal Arduino layer: Arduino.h (+Serial, GPIO), Stream.h,
                  FS.h (littlefs + directories), RTClib.h, LittleFS.h, sf32_platform.h
src/companion_ui/ the companion's screen UI: UITask.cpp is MeshCore's ui-new (changes
                  marked "SF32"); the board's screens, menus, settings and features are in
                  ui_*.inc, #included by it (see its header)
src/selftest/     bring-up self-tests
lib/              vendored Crypto, CayenneLPP, base64, and MeshCore (lib/MeshCore,
                  fetched by setup.sh, unmodified). See lib/README.md.
flasher/          sf32-flash.py: the stand-alone flasher for users (no SDK; downloads sftool)
tools/            sf32_debug_uart.py (flashing helper, crash debugging), shot.sh
                  (screenshots), touch.sh (remote touch, debug builds), pmstats.sh (sleep
                  counters), savelogs.sh (save the board's RAM logs), gen_font.py (the UI font),
                  gen_emoji.py (colour emoji), gen_logo.py (README banner), gen_boot_logo.py (boot screen logo)
sdk-patches/      the SDK fixes below (setup.sh applies them)
setup.sh          one-time setup: MeshCore, SDK fixes, SDK link
env.sh            activates the SDK through ~/sifli-lilygo-sdk (a symlink; the SDK's
                  linker flags break on paths with spaces). SF32_SDK_DIR picks the SDK.
licenses/, THIRD_PARTY.md   licenses of the bundled code and font
tdeck/            the LilyGo T-Deck port (in progress): a PlatformIO project; its own code
                  is src/tdeck/. See "T-Deck port" below.
```

Each firmware builds in its own folder (`project/`, `project_room_server/`,
`project_companion/`). The SDK scripts need bash or zsh; the project scripts start bash
themselves, so they also work from fish.

```sh
./build.sh selftest                                # bring-up self-tests; flash with ./flash.sh selftest
SF32_LOG_LEVEL=LOG_LVL_INFO ./build.sh companion   # keep log output (debugging)
```

## How the build fits together

- **Automatic builds**: `ci/build-all.sh` builds all three firmwares on a clean machine:
  it fetches pinned versions of LilyGo's SDK, the upstream SDK's `rtos/env/packages` and
  MeshCore (`MESHCORE_REF`, default `companion-v1.18.0`) into `.ci/`, installs the SDK's
  toolchain (needs `uv`), runs `setup.sh` and `build.sh`, and leaves
  `dist/<firmware>.zip` (bootloader.bin, main.bin, ftab.bin, main.elf and the license
  notices) next to a copy of the flasher. GitHub Actions (`.github/workflows/build.yml`)
  runs it on every push except docs-only ones and keeps those files as the run's artifacts.
  To move to a newer SDK or MeshCore, change the pins in `ci/pins.sh` (`setup.sh` uses the
  MeshCore pin too). Publish only these zips: a build on your own machine embeds its SDK
  path in the binary, which includes your home folder name.

- **Flasher checks**: `ci/test-flasher.py` tests `flasher/sf32-flash.py` without a board:
  sftool's download and checksum, firmware zips, port finding, error messages. GitHub
  Actions (`.github/workflows/flasher.yml`) runs it on Windows, macOS (Apple silicon and
  Intel) and Linux when the flasher changes, or by hand. It can't test a real flash.

- MeshCore sources are compiled with `-include sf32_platform.h` (Arduino.h, FS.h,
  `FILESYSTEM`) and the app's files also with `-include variant.h` (like a variant's
  build_flags). The companion also gets `-include sf32_companion.h`, which undefines the
  SDK's `BLUETOOTH` macro because it clashes with a UI enum.
- MeshCore's example `main.cpp`/`MyMesh.cpp`/`DataStore.cpp` pick a filesystem by platform
  and `#error` otherwise. They're compiled with RP2040_PLATFORM defined, *for those files
  only*, so they take the LittleFS branch, which `src/arduino/LittleFS.h` maps onto our
  littlefs volume. The companion's `<helpers/rp2040/SerialBLEInterface.h>` resolves to
  `src/platform/compat/`, which provides the SF32 BLE interface.
- SCons can't see force-included headers. `build.sh` hashes all headers under `src/` and
  drops the app/MeshCore objects when one changes. Without this, a stale object compiled
  against an older class layout overran its neighbour and crashed the companion at boot.
- The SDK's BLE registration macros don't compile as C++, so they live in
  `SF32SerialBLEInterface_events.c`.
- The SDK compiles with `-funsigned-char -fshort-enums`.

## Fixes the LilyGo SDK needs

In `sdk-patches/`, applied by `setup.sh`:

- `rtos/env/packages/` copied from the upstream OpenSiFli SDK (missing from the LilyGo
  download).
- Placeholder Kconfig files at `t_sf32_display_hw_device/sensor/bme280/` and
  `t_sf32_display_hw_device/uart_mux/gps/lwgps2rtt/`. LilyGo's Kconfig points to them but
  their repo doesn't contain them.
- `rtos/rtthread/bsp/sifli/drivers/drv_adc.c`: the two unconditional `rt_kprintf`s in the
  ADC read paths (`ch[7]voltage=...`) now need `DRV_ADC_DEBUG_PRINT`. They printed on every
  battery read and polluted the CLI/companion console.

## Flashing and debugging

sftool always connects at 1 Mbaud through the SF32's hardware debug-over-UART channel,
which listens at whatever baud UART1 runs at (115200 in this firmware). `flash.sh` first
runs `tools/sf32_debug_uart.py download`. That enters debug mode at the current baud,
halts the CPU (MeshCore's CLI would echo sftool's frames back), stops the hardware
watchdogs, and switches UART1 to 1 Mbaud for sftool. The host can't reset the board
(DTR/RTS do nothing); it boots right after flashing.

**Frame rate** (companion, measured on the board): the panel's QSPI clock is set to 60 MHz
(`LCD_MAX_CLK_FREQ` in `project_companion/rtconfig_project.h`), above the 50 MHz the SDK's driver
notes give for RGB565; a full frame takes about 15.4 ms. PSRAM (144 MHz) has bandwidth to spare, but
the CPU reaches it through its cache: one 480x480 frame took 15.5 ms to copy by CPU and 5.2 ms by
DMA, and a blend 38 ms by CPU and 8 ms by EPIC, the 2D engine. So frame backgrounds are DMA copies,
menu lists are EPIC blends (`composeList`, about 54-59 fps scrolling in Glass), and slides copy
nothing: the panel is sent the two frames' visible parts directly (about 57 fps), with their
length set in ms. On the SF32LB52 the LCD controller has one layer only (the SDK asserts this),
and EPIC takes 2 layers plus a mask per operation, so a Glass list takes two operations.

**Page drag** (`UITask::pageDrag`): past 26 px sideways (beyond what still counts as a tap), the page sticks to the finger. The page
coming in is rendered once, off screen (again if the finger crosses back over its start), and each
move just sends the two frames at the new offset. Let go past a third of the way, with a flick, or
after a quick swipe (60 px in under 350 ms) and it goes on; otherwise it springs back. Moves can
arrive in a batch while the page is being drawn, so the whole touch counts, not only the last speed. `tools/touch.sh drag` drives it in debug builds, but
screenshots read the last whole frame, so mid-drag frames can only be judged on the panel.
`tools/frames.sh` prints recent frame times in debug builds.

Before flashing, `flash.sh` saves what the board holds in RAM (`tools/savelogs.sh`, into
`board-logs/`, not in git): raw dumps of the sleep counters, test log, battery history and UI
state, the decoded versions, and the symbol table. It reads with the ELF kept in `.flashed/` by
the last flash, because a new build moves the addresses. If the board can't be read, it stops;
`SKIP_SAVELOGS=1 ./flash.sh ...` flashes anyway. After a test, `tools/savelogs.sh LABEL` saves
a named copy.

The same channel works on a running or crashed board:

| Command | Does |
|---|---|
| `tools/sf32_debug_uart.py PORT where` | Halt, print PC/LR/SP/MSP/PSP, resume |
| `tools/sf32_debug_uart.py PORT mem ADDR N` | Read memory, e.g. the fault registers at E000ED28 |
| `tools/sf32_debug_uart.py PORT shot ADDR out.png` | Screenshot (see below) |
| `tools/pmstats.sh` | Sleep counters from the last run on battery (see Power) |

- Never read XIP flash (0x1xxxxxxx) over this channel: the chip hangs until the watchdog
  reboots it. `mem` at a flash address reads `.flashed/main.bin` (the flashed image, kept by
  `flash.sh`) instead and doesn't touch the board.
- Use `arm-none-eabi-addr2line -e <build>/main.elf` on the addresses. A PC stuck in
  `rt_hw_hard_fault_exception` means a HardFault; the frame is on the PSP after the
  handler's saved registers.
- The debug channel answers its hello while the chip sleeps, but halts and memory access
  only land while it's awake (the companion stays awake on USB for this).
- A debug session left open by an interrupted sftool makes the chip answer the hello but
  drop every memory access; `enter()` in `tools/sf32_debug_uart.py` sends EXIT first to
  clear it.

## Screenshots and test builds

- `tools/shot.sh out.png` reads the frame on the panel (PSRAM, `sf32_shown_fb`); it halts
  the CPU for ~5 s and reads at 1 Mbaud. A thin stripe sometimes appears in a screenshot
  (the CPU caught mid-copy); it's not on the panel. Retake it.
- **Remote touch** (debug builds only): `SF32_REMOTE_TOUCH=1 ./build.sh companion`, then
  `tools/touch.sh press . up . tap 240 300 . right . hold 240 300 800` (taps at panel
  pixels, swipes, holds, button presses; "." waits). It writes a command word over the
  debug channel. Normal builds don't have the hook. Screenshots can't be limited the same
  way: they read memory through the chip's hardware debug port, the same one flashing
  uses, so anyone with the USB cable and the board in hand can read (or reflash) it
  whatever firmware it runs.
- **README screenshots**: add `SF32_SHOT_THEME=1` to the build: it shows Rainbow + Glass,
  the README's look, whatever theme is saved (and saves nothing), so all shots match.
- **Scripted UI**: `SF32_UI_DEMO=t_t_U_240,300 ./build.sh companion` plays touch input, one
  step every 8 s from 10 s after boot: `t` tap, `L` long press, `U`/`D` swipe up/down,
  `l`/`r` swipe left/right, `X,Y` tap at panel coordinates, `.` wait.
  `SF32_UI_DEMO_TIME=<epoch>` also sets the RTC at boot. Test builds skip power off and
  reboot, and never transmit an SOS.
- Other flags: `SF32_SUN_TEST=1` (New York as the location for sun times),
  `SF32_SPLASH_TEST=1` (the boot screen stays 2 min, for screenshots),
  `SF32_PM_LOG=1` (sleep statistics on the console), `SF32_FRAME_LOG=1` (frame timing),
  `SF32_MOTION_LOG=1` (accelerometer every second), `SF32_SOUND_LOG=1`,
  `SF32_I2C_SCAN=1` (I2C scan at boot), `SF32_SENSOR_PROBE=1` (the motion sensor's virtual
  sensors at boot), `SF32_RAINBOW_TEST=1` (Rainbow theme, not saved).
- Bench tests that take the board over: `SF32_NOISE_TEST=1` (RSSI with the screen and audio
  on and off, in 40 s phases), `SF32_CPU_TEST=1` (a line of CPU, clock and RSSI readings
  every 2.5 s), `SF32_CPU_TOGGLE_TEST=1` (screen off, CPU alternating 96 / 240 MHz every
  15 s), `SF32_SOUND_STRESS=1` (a tap sound every second and a volume change every 4 s; it
  changes the saved Sound setting, so check that afterwards).

Reflash the normal build after using a test build.

## Display

MeshCore's UIs are laid out for a 128x64 OLED. `SF32Display` keeps that layout but draws it
at 3.5 panel pixels per OLED pixel (448x224, centred on the 480x480 panel) straight into the
frame buffer:

- Text in anti-aliased Noto Sans Medium (SIL OFL), pre-rendered per text size by
  `tools/gen_font.py` into `src/platform/SF32Font.h`. It's proportional, and
  `getTextWidth()` reports the real width, so MeshCore's centring and ellipsizing still fit.
- Icons are scaled smoothly, small square dots become round, and big panels get rounded
  corners; full-width bars extend into the margin.
- `printWordWrap` wraps at word boundaries (message view).
- "press Enter" hints read "long press", the actual gesture on this board.

To change the font or scale: edit `SCALE`/`FONT` in tools/gen_font.py (needs Pillow, run
with the system python) and `SF32Display::SCALE`, then rebuild.

The driver double-buffers (frames alternate between two PSRAM buffers; a frame is sent to
the panel in the background while the next is drawn, ~40 ms per frame) and animates
transitions by composing 6 in-between frames in a third (~250 ms). Long lists are drawn
once into an off-screen layer and copied per frame while dragging.

**Rotation**: the panel turns the picture itself (CO5300 MADCTL row/column swap and
mirrors: free, partial updates included; the SF32LB52's LCD controller can only mirror top
to bottom). Touches turn with it, and it's set again after the panel powers up (waking
re-initialises it). Axes are calibrated in SF32Motion.cpp (`ROT_AXIS`, `ROT_SIGN`,
`ROT_SIDE_POS`).

Burn-in care: the whole layout moves by up to 3 pixels every 3 minutes, and the always-on
clock moves each minute.

## Power and sleep

- **Light sleep** (companion; Settings > Sleep, on by default): RT-Thread's power
  manager (`CONFIG_BSP_USING_PM`) light-sleeps the chip whenever it's idle with the screen
  off (always-on counts as on), the audio off and the first minute after boot over. The
  policy is light sleep only (SF32Board.cpp overrides the SDK's weak `pm_policy[]`: the 52x
  default is deep sleep). The 25 ms screen-off main loop, the radio, touch, button and
  Bluetooth all wake it.
- LilyGo's board support powers the LoRa radio off in sleep (`BSP_PowerDownCustom`):
  overridden, since a mesh node must keep listening.
- It stays awake on USB power, which keeps flashing and screenshots working. Sleep Off
  holds it awake (the way back if something misbehaves). `SF32_PM_LOG` test builds sleep on
  USB too, after 8 minutes; to flash one, tap the screen on first.
- **Sleep counters**: every companion build counts, from unplugging until USB comes back,
  the time asleep, wakes and their sources (the wake source register's bits), time held
  awake and main loop passes (`sf32_pm_stats` in SF32Board.cpp). Plug back in and run
  `tools/pmstats.sh`. An overnight run with the motion sensor off measured 96 % asleep and
  38 wakes/s from the main loop timer (LPTIM1), with almost none from pins; the sensor had
  been causing 11 pin wakes/s.
- **Battery tests (2026-11)**: one change per night, on battery, in alternating 45 min
  stretches (2 h stretches got confused with the battery's discharge curve), each compared
  with the stretches either side. The test-only build flags and `abstats.sh` were removed
  once the work was done; the logs are kept locally. The counters above also record radio
  and touch interrupts and the audio chip powering down/up (`pmstats.sh` prints them).
- **LilyGo's power report**
  ([`lowpowertest/`](https://github.com/Xinyuan-LilyGO/T-Display-SF32/tree/master/lowpowertest)):
  0.87 uA shut down, 164 uA hibernating, so regulators and the charger aren't where the
  sleep draw goes.
- **CPU clock scaling** (without the power manager): with the screen off for 5 s, the CPU
  drops from 240 to 96 MHz at the lower core voltage (SiFli DVFS S0) and the main loop
  wakes every 25 ms instead of 4.
- The idle thread uses WFE, the SDK's own idle instruction; WFI missed the BLE
  controller's wake-up events and broke Bluetooth.
- Bluetooth advertises every 30 ms for 30 s after boot or a disconnect, then every 500 ms.
- **Battery history**: a sample every 15 min for 48 h in `/sf32_batt` (charge, USB, raw
  voltage, chip temperature), saved hourly and before powering off. Time left is a straight
  line fitted through up to 12 unplugged hours, skipping the first half hour off the
  charger.
- **Battery protection**: on battery below 3550 mV for 30 s, the board powers off
  (SGM41562B shipping mode). The voltage is a rolling average of one ADC reading every 2 s.
  If the charger cycles, as it does with no battery, the board reports 0 V. The ADC clips
  at about 4.3 V, so a full cell reads ~4.28 V. Never put the charger in HIZ from firmware:
  with no battery it cut power and latched until USB was replugged.

## Sound, sensors, diagnostics

- **Sound**: `SF32Sound` generates tones and plays them AUDPRC -> AUDCODEC DAC -> NS4150B
  amplifier (PA20), in its own thread. The audio path takes 100+ ms to come up and pops
  when it powers up, so while Volume or Taps is on it stays open (silent) while the
  screen is in use and for 2 min after, then powers down. A startup chime covers the boot
  pop. `rt_device_write` is called with interrupts off: the RT-Thread audio driver could
  otherwise block forever when the DMA interrupt won a race.
- **Motion** (`SF32Motion`): LilyGo's BHI260AP driver uploads Bosch's firmware at boot
  (background thread); the accelerometer runs at 10 Hz only while lift to wake, flip to
  quiet or auto rotation is on.
- **I2C**: i2c1 has 0x03 SGM41562B charger, 0x28 BHI260AP IMU, 0x5a CST9220 touch. i2c2
  and i2c4 are empty. No vibration motor and no light sensor on this board.
- **Logs**: `/sf32_boot` records why the board last stopped (power-off reason, reboot, or
  "running" left over = power lost or a reset; flashing looks like the latter). Events go to
  `/sf32_log` (last 20, about 3 KB), shown in Settings > Logs. Each entry stores a snapshot
  after a 0x1F byte: uptime, battery mV, USB/charge state, chip temperature, free heap and
  its low point, CPU MHz; starts add the power-on mode and PMU wake status.

## Bluetooth and USB

- Nordic UART service, MTU 517 (frames up to 176 bytes).
- The device asks a newly connected phone to pair only if it doesn't start pairing itself
  within 1.5 s (an earlier request collided with the phone's own and dropped the link).
- Security level 3 forbids unauthenticated pairing. The stack reports bonded re-encryption
  as auth 0x09 (no MITM bit), so "connected" means encrypted with Secure Connections. A
  phone counts as connected only once paired, so the PIN stays on screen until then.
- The BLE CCCD must stay unencrypted, or Android never enables notifications.
- **PC link** (Settings > PC link; `SF32UsbInterface`): MeshCore's `<` / `>` framing over the console UART,
  alongside Bluetooth. It counts as connected only after a valid frame arrives (MeshCore's
  stock serial interface is always "connected", which would hide the BLE PIN); console logs
  are paused while connected, and it disconnects when USB power goes away.

## Radio

LilyGo C RadioLib quirks worked around in SF32SX1262Radio.cpp:

- Its SPI layer waits for BUSY low *before* asserting NSS, so it can't wake a sleeping
  SX1262. `wakeFromSleep()` pulses NSS.
- `SX126x_scanChannel()` issues SetCAD from RX; the chip only accepts it in standby.
- `SX126x_startReceive()` doesn't latch PREAMBLE_DETECTED/HEADER_VALID, which MeshCore
  needs, so the wrapper has its own receive start.
- SNR is kept in whole dB; the wrapper reads packet status for 0.25 dB.

TCXO 3.0 V, DC-DC regulator, DIO2 RF switch, PA26 enable (as LilyGo's examples).

## Other notes

- Console: uart1 at 115200 via the CH343P, msh disabled, logs filtered (warnings for
  repeater/room server, none for the companion, whose console can carry binary frames).
  LilyGo's LCD driver still prints `CO5300_ReadID 0x0` when the panel powers on.
- Watchdog: fed from MeshCore's loop. It reboots about 1 min after a stall (two stages).
- RNG: identities mix the SF32 hardware TRNG with SX1262 wideband noise.
- Clock: on-chip RTC (32.768 kHz crystal). The companion's `bootstrapRTCfromContacts()`
  sets the clock to the newest contact's timestamp at every boot, which threw a synced
  clock back to 2024, so SF32RTCClock never moves a synced (2025+) clock backwards, like
  MeshCore's own time commands. `clock reset` still does.
- Storage: littlefs on the 4 MB `FS_REGION` partition (0x128A0000), formatted on first boot.
- Main thread stack is 16 KB (the SDK's 2 KB default overflows in Ed25519).
- MeshCore's contacts API: `getContactByIdx()` counts 8 reserved anonymous slots that
  `getNumContacts()` doesn't; iterate with `startContactsIterator()`.

## Measurements

- Crypto (192 MHz): Ed25519 sign 4 ms, verify 47 ms, ECDH 7 ms.
- Noise floor: about -106 dBm with the battery on (-103 with it off) vs -115 on a T1000-E
  nearby. It's broadband and unaffected by the SX1262 regulator mode or CPU load; a bright
  screen adds a few dB. There's a spur at 912 MHz (19 x 48 MHz system clock), 1.5 MHz from
  the US preset.
- Battery, screen mostly off: about 37 mV/h (roughly 22 h) before the work below, about
  29 mV/h (roughly 28-30 h, about 16-17 mA if the battery really is 480 mAh) after it.
- What the overnight tests found (2026-11):
  - **Touch controller scanning** was the biggest screen-off draw: in deep sleep it drained
    about 60 % less in those stretches. Now asleep while the screen is off unless Tap to
    wake is on (its low-power scan mode reports no touches, so it's sleep or full scanning);
    repeater and room server put it to sleep at boot.
  - **Audio chip**: the SDK leaves its PLL, reference and bandgap on after the first sound,
    and the crystal's audio buffer on from boot: about 2 mA. Now powered down with the
    screen off (`SF32Sound::powerSave`).
  - The **LoRa radio** listening is about 5 mA (asleep, the drain fell 30-40 %); it stays
    listening by choice.
  - No measurable change: motion sensor off, Bluetooth advertising off, light vs deep
    sleep, a 250 ms screen-off main loop (fewer wakes and much less time awake moved
    nothing), the console UART pins released. Not fitted: microSD. Too small to matter: the
    SPI MISO pull-down (PA25).
  - What's left (about 11 mA) is spread across the chip and the board's always-on parts.

## T-Deck port (in progress)

The LilyGo T-Deck (ESP32-S3) build is a PlatformIO project in `tdeck/`. Like the SF32 build it
compiles MeshCore (`lib/MeshCore`) unmodified: `tdeck/platformio.ini`'s `build_src_filter` picks
MeshCore's core, the ESP32 helpers, its ed25519 and the companion_radio example. Our code is
`src/tdeck/`: TDeckBoard (based on MeshCore's `variants/lilygo_tdeck`), target.h/.cpp, and
TDeckContacts. Pins and radio settings follow MeshCore's T-Deck variant.

The companion UI (`src/companion_ui`) and the display code (`src/platform/SF32Display.cpp`) are
shared with the SF32, not copied:
- `src/tdeck/compat/` supplies the few RT-Thread and SF32 HAL calls that code makes. Its "lcd"
  device is the ST7789 (`TDeckPanel.cpp`, synchronous sends on the shared bus) and its "rtc" the
  ESP32's clock (`TDeckDevices.cpp`), so `SF32Display.cpp` and `SF32RTCClock.cpp` compile as
  they are. `tdeck_platform.h` is force-included (InternalFS = the SPIFFS data partition).
- `TDeckBoard` also has `SF32Board`'s interface (and is called that here); `TDeckRadio` adds the
  SF32 radio's extras to MeshCore's SX1262 wrapper; `TDeckBLE` adds the pairing window etc. to
  MeshCore's ESP32 Bluetooth.
- Touch: the shared `SF32TouchInput.cpp` (gestures, taps, drags) reads a "touch" device that is
  the GT911 (`TDeckTouch.cpp`, polled from the main loop every 15 ms; its axes swapped, y mirrored).
  Its distances are per board (`SWIPE_MIN_PX`, `TAP_MAX_PX`, `TOUCH_EDGE_L/R`).
- `TDeckStubs.cpp`: silent stand-ins for what isn't ported yet (sound, the USB app link;
  the T-Deck has no motion sensor).
- `SF32Display.cpp` changes for the T-Deck are behind `VESPER_TDECK` (its buffers come from PSRAM
  there). The font, emoji and logo are generated at the T-Deck's scale (1.875x) into
  `src/tdeck/TDeck*.h` (`SCALE=1.875 OUT=... tools/gen_font.py` etc.).
- Keep the SF32 build unchanged when touching shared code: build the companion with a fixed
  `SOURCE_DATE_EPOCH` before and after and compare `main.bin` (byte-identical so far).

For now the UI is the SF32's 128x128 layout at 1.875x, centred (layout B, widening it to fill
the 320x240 screen, comes next). Touch works. Trackball and keyboard (`TDeckKeys.cpp`): rolling
left/right = previous/next, up/down = the swipe menus on the home pages and up/down elsewhere
(one step per roll), the click = the SF32's button A; Enter selects, Backspace goes back, Space
is next.

```sh
cd tdeck
./build.sh      # PlatformIO (~/.platformio/penv/bin/pio or pio on PATH)
./flash.sh      # saves the data partition first, then flashes; finds the T-Deck by its USB id
./monitor.sh    # USB serial
./build.sh -e tdeck_debug && ENV=tdeck_debug ./flash.sh   # debug firmware: screenshots
python3 shot.py ../shots/x.png                           # (debug firmware only)
```

- Flashing writes the bootloader, partition table, boot_app0 and the app only. The data
  partition (SPIFFS at 0xc90000, 3.4 MB, MeshCore's usual place on the T-Deck) is kept, so a
  T-Deck coming from MeshCore or meshcomod keeps its identity, contacts, channels and settings.
- `flash.sh` saves the data partition to `board-logs/` first (`save_data.py`), at most once a
  day: reading it takes ~6 minutes over the T-Deck's USB, and flashing the app never writes it.
  The copies hold the private key. It refuses a normal build that has the screenshot tool.
- Contacts: firmwares like meshcomod keep more than MeshCore's 350. MeshCore would load the first
  350 in file order and drop the rest at its next save, so `TDeckContacts` trims the file once at
  boot, before MeshCore reads it: favourites, then the most recently updated. The full file
  stays as `/contacts3.full`.
- The screen, radio and microSD share one SPI bus: all three chip selects (9, 12, 39) go high
  first, and everything uses the one SPIClass that the radio starts.
- A build that crashes before USB comes up: hold the trackball down and press reset (download
  mode), then flash.

