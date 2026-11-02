# MeshCore for the LilyGo T-Display-SF32

MeshCore firmware for the LilyGo T-Display-SF32 (SF32LB52 + SX1262 LoRa + 480x480 AMOLED):
**companion** (phone app over Bluetooth, with a touch UI), **repeater** and
**room server**. It's built on the LilyGo SiFli SDK and LilyGo's C RadioLib port.
MeshCore itself is used unmodified.

## Getting started

You need Linux or macOS with bash/zsh, git, and the board on USB.

1. Get the LilyGo SiFli SDK v2.5.1 (https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo,
   folder `sifli-sdk-v2.5.1`) and install its toolchain as LilyGo describes. Also get the
   upstream SDK (`git clone https://github.com/OpenSiFli/SiFli-SDK`): LilyGo's download
   lacks `rtos/env/packages`.
2. `./setup.sh /path/to/sifli-sdk-v2.5.1 /path/to/SiFli-SDK` fetches MeshCore into
   `lib/MeshCore` (pick a version with `MESHCORE_REF=<tag>`; developed against v1.18.0),
   applies the SDK fixes in `sdk-patches/`, and links the SDK at `~/sifli-lilygo-sdk`
   (its build breaks on paths with spaces).
3. `./build.sh companion && ./flash.sh companion`.

Serial port permission on Linux: add yourself to the `uucp`/`dialout` group, or a udev
rule such as `SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", MODE="0666"`.

## Usage (bash/zsh; the SDK scripts don't support fish)

    ./build.sh [repeater|room_server|companion]   # default: repeater
    ./flash.sh [repeater|room_server|companion]   # flash over USB (auto-picks the port)
    ./monitor.sh                                  # serial console at 115200; Ctrl+] to quit

    ./build.sh selftest   # bring-up self-tests (steps 1-4), flash with ./flash.sh selftest
    SF32_LOG_LEVEL=LOG_LVL_INFO ./build.sh companion   # keep log output (debugging)

Each firmware builds in its own folder (`project/`, `project_room_server/`,
`project_companion/`). From fish, run the scripts as normal; they start bash themselves.
Settings and the node identity live on the board's flash and survive reflashing.
All three firmwares share the same identity file.

**Power switch**: the red I/O switch on the board disconnects the battery. With it on
**O**, the board only runs on USB, loses its clock on unplug, and the charger cycles with
nothing attached. Keep it on **I**.

## Repeater and room server

Configure them with https://config.meshcore.io (Chrome/Edge, USB, CH343P port). The board
speaks MeshCore's stock serial CLI at 115200 baud. Over LoRa, use Remote Management in
the MeshCore app.

Defaults (src/platform/variant.h): USA/Canada preset (910.525 MHz, BW 62.5, SF7, CR 4/5),
22 dBm. Repeater "SF32 Repeater"; room server "SF32 Room", room password "hello". Admin
password **"password"**: change it.

Screen: MeshCore's repeater/room screen (see "Display" below). It turns off after 20 s. Button **A** turns it on; holding A for 1 s powers the board off. To power
on, press **D** (the charger's button on the bottom edge, below the antenna) or plug in USB.

Board-specific CLI commands:
- `clock reset` (USB only): RTC back to MeshCore's default start date. MeshCore never
  moves the clock backwards, so this is the only fix for a clock set in the future.
- `battery`: battery voltage, spread over 20 samples, USB, charger state and faults.
- `charger`: raw SGM41562B registers 00-0B, plus the HIZ / CEB / FET_DIS bits.
- `charger test` (USB only): pauses charging for 3 s and reads VBAT (0 V = no battery).
- `buttons`: levels and change counts for buttons C (PA33), A (PA34), B (PA35).
- `display on` / `display off`.
- `noise` / `noise test` / `noise scan FROM TO STEP` (kHz; USB only): radio background
  level; test = regulator, RX gain, CPU load, screen states, a 902-928 MHz sweep and a
  fine sweep around the channel; scan = average per frequency with the screen off.
- `wdt test` (USB only): stalls the main loop; the watchdog reboots in about a minute.

## Companion (MeshCore phone app)

Bluetooth: advertises as `MeshCore-<name>` with the Nordic UART service. On first
connect, the phone asks for a PIN: it's the 6-digit **Pin:** shown on the screen, new
each boot until a phone is paired. It uses LE Secure Connections passkey pairing with
bonding, so later connections need no PIN. A phone counts as connected only once it's
paired, so the PIN stays on screen until then. The negotiated MTU is 517 (frames up to 176 bytes).
No companion protocol over USB: MeshCore treats a USB link as always connected, which
would hide the PIN (stock Bluetooth companion builds leave it out too).

Screen and input: MeshCore's companion UI (ui-new), from our copy in `src/companion_ui/`
with two extra pages:
- **Clock** (first page, and where the screen goes back to when it turns off): time and
  date in your time zone, an envelope with the number of messages not yet viewed on the
  device, battery % and voltage (bolt = on USB power), and the BLE PIN until a phone is
  paired (then the node name). Long press: show the unread messages. The time comes from
  the phone app when it connects; until then it shows "--:--".
- **Time zone** (Settings > Clock > Time zone, or the clock page's menu): **-** / **+**,
  **Done** (button A: click = +1 h, double click = -1 h, long press = done).
  The phone can't set it: MeshCore's companion protocol only sends UTC. With **Daylight
  saving** set to US or EU (Settings), the zone is your standard time (e.g. UTC-5 for US
  Eastern) and summer time is added automatically.
- **Radio editor** (Radio page, swipe up, "Edit radio"): one card per setting
  (Preset, Frequency, BW, SF, CR, TX power). Tap one to open its own screen: saved vs new
  value, **-** / **+** (frequency also has a step size: 0.025/0.1/1/0.001 MHz, limited to
  902-928 MHz), **Done**. Changed settings are tinted amber and the button reads "Apply N
  changes"; Apply lists the changes (old -> new) to confirm. Nothing reaches the radio
  until then. Left/right swipes do nothing here; swiping up/down with unsaved changes asks
  Keep / Discard / Apply, and the screen wakes back into the editor however long it was
  off. Presets: MeshCore's USA/Canada, Australia and New Zealand (narrow). The other
  nodes must use the same settings. Whole hours, so change it by hand for daylight saving. It's MeshCore's own
  `tz_offset` setting (also `set tz.offset N` via the app's CLI).

Every screen uses a "MeshCore" theme (MeshCore's slate #1F2937, a blue accent, black
background): page title with an accent bar, compact battery, content on rounded cards,
page dots. Pages: Clock, Status, Battery, Heard, Radio, Bluetooth, Advert, Bot, Discover,
Signal, Activity, Power.

Controls:
- Button **A** / **tap**: next page. Double tap: previous page. Long press: select.
- **Swipe** left/right: next/previous page. Any touch wakes a dark screen.
- Pages slide left/right when changing; menus slide down (power) or up (page menu).
- **Swipe right** (finger left to right) in Settings and the screens under it: back one
  level.
- **Swipe down**: power menu (Flashlight, Screen off, Reboot, Power off, Send SOS).
  **Flashlight**: the whole panel lit at full brightness, white or red (tap switches; red
  keeps night vision), 5 min unless touched; swipe or button A turns it off.
- **Swipe up**: menu for the current page (e.g. Bluetooth: turn off, forget paired
  phones; Advert: send; Message: mark all read), plus **Settings**.
- In menus, tap a row. With button A: click = next row, long press = choose. Swipe
  up/down or tap below the rows to close.
- **Clock faces**: Digital, Analog, Big (stacked hours/minutes), Info (time + unread,
  battery, phone, last heard cards) and Color (a rainbow ring of minute dots filling up
  through the hour, a colour per digit, on true black) and Sun (the day as an arc from
  sunrise to sunset with the sun on it, a pale moon below at night, the times and how long
  until the next). Sunrise / sunset come from the node's location as set in the app
  (ui_sun.inc, the sunrise equation; no network); the Info face shows the next one too.
  Long press the clock, or its swipe-up menu > Face, or Settings > Display > Clock face.
- **Settings** is in this order: what you see, what you hear, sensors, radio, system.
  **Display** (theme, clock face, brightness, timeout, always-on), **Pages** (show/hide
  each home page; the clock always shows; swipes and page dots skip hidden ones),
  **Clock** (time zone, DST, format, mesh time, reset), **Sound** (volume, alerts, tap
  sound), **Night mode** (from, until, **Dim** and how far, **DND**, do not disturb:
  messages make no chime and don't light the screen, they still count as unread; "DND"
  shows on the clock while active), **Motion** (lift to wake, flip to quiet), then
  Bluetooth, SOS to, Low power, Logs, Power. Back rows or a swipe right go up one level.
  Details: brightness
  (25/50/80/100%), **Always-on** (instead of going dark: the time, date and an unread
  envelope on black at 6 % brightness, redrawn once a minute and moved to a new spot each
  minute; touch, button A or picking it up wakes the full UI; face down still turns it
  off), **Night dim** (a lower brightness, 5/10/25/50 %, between two hours,
  e.g. 10 PM to 7 AM; needs the clock set; there's no light sensor for real auto
  brightness), timeout (15 s, 30 s, 1 min, 5 min, or never while on USB power),
  **Lift to wake**, **Flip to quiet** (motion, see below), **Sound** submenu: **Volume**, **Tap sound**, **Alerts** (new-message alerts for all messages / direct only / off: a chime and the
  screen lights up for 6 s, also while the phone is connected), **Sound** (volume for chimes, tap and
  swipe sounds: off / low -24 dB / medium -6 dB / high 0 dB; changing it plays a test chime), **Theme** (9 colour schemes: MeshCore blue, cyan, teal,
  green, purple, pink, red, orange, mono, and Rainbow: every home page in its own hue; tap a swatch to apply it), **Clock** (format
  12/24 hour, time zone, DST = daylight saving off/US/EU), Bluetooth (remembered across reboots and power-offs). Saved in `/sf32_ui` on the board.
- Long lists (Settings, About) follow your finger when dragged and glide on after a flick
  (drawn once into an off-screen layer, then copied per frame). Swipe down with the list
  at the top to close it; Close/Back rows too. Menus go back the way they came: the power
  menu (down from the top, with a handle at the bottom) closes with a swipe up (once it's
  scrolled to its end: it has 5 rows); menus that came up from the bottom close with a
  swipe down.
  Labels too long for their row scroll sideways inside it.
- **Bot** page (off by default; tap anywhere to turn it on/off): auto-replies to messages
  sent with **1-byte path hashes**, asking the sender to switch to 2-byte. The hash size
  comes from the received flood packet's path length byte (bits 6-7, set by the sender's
  path hash mode); direct-routed packets are skipped. Swipe up for options: **Auto-reply**
  (on/off), **To DMs** (yes/no) and **Channels** (off / **DM back** to the sender if they're
  in your contacts / **Post** the reply in the channel as `@[name] ...`). Each sender gets it at most once a
  day, at most one reply a minute, 3 s after the message, never to auto-replies. The page
  shows the reply count and who got the last one; each reply is also listed in Logs.
- **Battery** page: charge, voltage, the chip's temperature (the SoC's own sensor, in °F: a few
  degrees above the room, more while charging), charging / estimated time left until the
  low-battery power-off (a straight line fitted through the charge, worked out from each
  sample's voltage, over up to the last 12 unplugged hours; the first half hour off the
  charger is left out, the voltage sags faster than the battery empties; "estimating..."
  until there's an hour of it) and a 48 h graph (a sample every 15 min; plugged-in
  stretches in green, time powered off as gaps). Kept in `/sf32_batt` (each sample also stores the raw
  battery voltage and chip temperature, for power tests), saved hourly and before powering off.
- **Activity** page: packets heard in the last hour and 24 h, how busy the channel is
  (share of the time the radio spent receiving or sending, this hour) and a bar graph of
  packets per hour. From the radio's own counters, no extra traffic; RAM only (starts over
  at boot).
- **Auto advert** (Settings): send adverts on a schedule, as repeaters do, so other nodes
  keep a fresh path: **Nearby** (zero hop) off / every 1, 3, 6, 12 h and **Mesh** (flood)
  off / every 12, 24, 48 h, both off by default. Advert / Heard / Radio page menus: **Send
  nearby** or **Send to mesh** now; the Advert page's menu > **Long press** picks what its
  long press sends (Nearby / Mesh).
- **Signal** page (signal tester): tap to start/stop. Every 10/15/30/60 s (swipe up:
  Every) it pings the repeaters in direct range (zero-hop discovery request) and shows how
  well the best one hears us and we hear it (SNR), its name, replies/pings and a bar graph
  of the last 40 pings (no reply: red tick). A beep per ping, higher for a better link, two
  low beeps for no reply (follows the Sound setting). Swipe up > Target locks onto the
  repeater just heard. The screen stays on while testing; it stops by itself after 30 min.
- **Mesh SOS**: triple-press button A, or swipe down > Send SOS. A 5 s countdown with
  beeps (Cancel button, or press A), then it sends "SOS! I need help. Location: lat,lon.
  Battery N%." to the channel in Settings > SOS to (default: the first channel,
  usually Public), and again every 5 min, 6 times in all, until you tap Stop. The location
  is the node's as set in the app (or "unknown"). Logged in Logs. Scripted test builds
  never transmit it.
- **Mesh time** (Settings > Clock, on by default): while the clock isn't known (battery
  ran flat, battery switch, fresh flash), the times in received adverts set it once 3
  different nodes agree within 5 minutes (their middle value, 2 min behind so the phone,
  which MeshCore only lets move the clock forward, can still correct it). The clock page
  shows "(mesh)" after the date until the phone syncs. Only nodes already in your
  contacts count (new identities cost nothing to make); unsynced nodes (before 2025) and
  outliers are ignored. **Reset clock** (Settings > Clock, tap twice) sets the clock back
  to "unknown", the way out of any wrong time ahead (MeshCore only moves it forward).
- **Share QR**: swipe up on the Clock or Status page > Share QR shows this node
  as a QR code (`meshcore://contact/add?name=...&public_key=...&type=1`, MeshCore's
  docs/qr_codes.md) for the MeshCore app to scan. Full brightness while shown, a minute
  before the screen times out; tap, swipe or press A to close. QR encoder: Nayuki's
  qrcodegen (MIT, src/qrcodegen, from the SiFli SDK's LVGL).
- **Band scan** (Radio page > swipe up): the noise level across 902-928 MHz in 250 kHz
  steps, swept continuously (bars coloured by level, faint max-hold marks, our channel
  marked, the strongest frequency and our channel's level below; tap to reset). A few
  frequencies per frame, then back to our channel, so MeshCore keeps receiving. Narrow
  spikes: a device's carrier or a clock harmonic (this board's own: 912.00 MHz = 19 x 48
  MHz); a broad hump: noisy electronics.
- **Power saving / sleep** (companion, stage 1; Settings > Low power, on by default):
  RT-Thread's power manager (`CONFIG_BSP_USING_PM`) light-sleeps the chip whenever it's
  idle with the screen off (always-on clock counts as on), the audio off and the first
  minute after boot over. The policy is light sleep only (SF32Board.cpp overrides the SDK's
  weak `pm_policy[]`: the 52x default is deep sleep). The 25 ms screen-off main loop, the
  radio, touch, button and Bluetooth all wake it; measured ~88% of the time asleep, the
  radio sending and receiving throughout. LilyGo's board support powers the LoRa radio off
  in sleep (`BSP_PowerDownCustom`): overridden, a mesh node must keep listening. The CPU
  clock scaling below is bypassed while the power manager is on. Low power Off holds the
  chip awake (the way back if something misbehaves). It also stays awake while on USB power
  (charging), which keeps flashing and screenshots working: the debug channel answers its
  hello while the chip sleeps, but halts and memory access only land while it's awake.
  (SF32_PM_LOG test builds sleep on USB too, after 8 minutes; to flash one, tap the screen
  on first.) A debug session left open by an interrupted sftool makes the chip answer the
  hello but drop every memory access; `enter()` in `tools/sf32_debug_uart.py` sends EXIT
  first to clear it.
- **CPU clock scaling** (without the power manager): with the screen off (and no audio) for 5 s, the CPU drops from 240 MHz
  to 96 MHz at the lower core voltage (SiFli DVFS S0; the RT-Thread tick is reconfigured)
  and the main loop wakes every 25 ms instead of 4. Full speed returns before the screen
  turns on or audio starts. Verified: timekeeping, radio and I2C unaffected; the radio's
  noise is the same at either speed.
- **Tap sound** (Settings): a sound from the speaker on taps (Click: a soft wooden tock,
  Pop: a water-drop bloop, Tick: a crisp tick; tapping the row cycles them and plays each)
  and a whoosh (airy filtered noise that swells and fades) on swipes, or Off. A short startup
  chime (E B E rising) plays at boot when Sound or Tap sound is on (switching the audio on pops; the pop then
  starts the chime). At the Sound volume (Low if Sound
  is off); the board has no vibration motor. The audio path takes 100+ ms to come up, too
  slow for a tap sound, and powering it up pops: so while Volume or Tap sound is on it
  stays open (silent) while the screen is in use and for 2 min after it goes off (or to the
  always-on clock), then powers down (a few mA). The first wake or chime after that powers
  it up again (one pop); at boot that's under the startup chime.
- **Discover**: tap anywhere to scan. **Power** page: tap anywhere for the power menu
  (powering off takes a second tap). Use a swipe or button A to leave these pages.
- Unread messages: they count as read once the phone app downloads them (the app has no
  read receipts; downloading is the only signal), or when viewed on the board.
- A new message that alerts (Settings > Sound > Alerts) shows a small banner at the top for
  6 s, over whatever is on screen: the sender and the start of the text. The full text is in
  the message view (the clock's envelope / "Read messages"). The device doesn't send
  messages: no typing or replies on it, by design; that's the phone's job. Tap the
  envelope on the clock to open the messages; swipe left for the next, swipe right to go
  back (they stay unread for the app).
- Woken within 10 s of turning off, the screen comes back where it was (menus and
  editors included); after that, on the clock. Settings ends with **Power** (screen off,
  reboot, power off).

If a phone was paired with older test firmware, remove the device from the phone's
Bluetooth settings first. The firmware cleared its own old bonds once.

## Display

MeshCore's UIs are laid out for a 128x64 OLED. `SF32Display` keeps that layout but
draws it at 3.5 panel pixels per OLED pixel (448x224, centred on the 480x480 panel)
straight into the frame buffer:
- text in anti-aliased Noto Sans Medium (SIL OFL), pre-rendered per text size by
  `tools/gen_font.py` into `src/platform/SF32Font.h`. It's proportional, and
  `getTextWidth()` reports the real width, so MeshCore's centring/ellipsizing still fits.
- icons are scaled smoothly (interpolated contour), small square dots become round, and
  big panels (title bar, popups) get rounded corners; full-width bars extend into the
  margin so text isn't on their edge.
- `printWordWrap` wraps at word boundaries (message view).
- "press Enter" hints read "long press", the actual gesture on this board.
- dark palette in SF32Display.cpp (`UIColor`).

To change the font or scale: edit `SCALE`/`FONT` in tools/gen_font.py (needs Pillow, run
with the system python) and `SF32Display::SCALE`, then rebuild.

The driver double-buffers (frames alternate between two PSRAM buffers; a frame is sent to
the panel in the background while the next is drawn, ~40 ms per frame) and animates
transitions (`setTransition`) by composing 6 in-between frames in a third (~250 ms).

Screenshots: `tools/shot.sh out.png` (or `tools/sf32_debug_uart.py PORT shot
@<address of sf32_shown_fb> out.png`) reads the frame on the panel
(PSRAM, `s_fb` in main.map); it halts the CPU for ~5 s and reads at 1 Mbaud. `SF32_UI_DEMO=t_t_U_240,300 ./build.sh
companion` builds a test firmware that plays touch input, one step every 8 s from 10 s
after boot: `t` tap, `L` long press, `U`/`D` swipe up/down, `l`/`r` swipe left/right, `X,Y` tap at
panel coordinates, `.` wait. `SF32_UI_DEMO_TIME=<epoch>` also sets the RTC at boot. Reflash the normal
build afterwards.
Live remote touch (debug builds only): `SF32_REMOTE_TOUCH=1 ./build.sh companion`, then
`tools/touch.sh press . up . tap 240 300 . right` (taps at panel pixels, swipes, button
presses; "." waits) writes a command word over the debug channel. Normal builds don't
have the hook. Screenshots can't be limited the same way: they read memory through the
chip's hardware debug port, the same one flashing uses, so anyone with the USB cable and
the board in hand can read (or reflash) it whatever firmware it runs.

## Sound, sensors, diagnostics

- Motion (`SF32Motion`, companion): LilyGo's BHI260AP driver uploads Bosch's firmware at
  boot (background thread) and the accelerometer runs at 10 Hz. **Lift to wake**: lying
  still for 1.5 s, then tilted by more than ~20 degrees turns the screen on (walking or a
  pocket doesn't, it's never still). **Flip to quiet**: face down for 1 s turns the screen
  off and silences message alerts until it's turned over. Both in Settings.
  `SF32_MOTION_LOG=1 ./build.sh companion` prints the accelerometer every second.
- Chimes: `SF32Sound` generates short tones and plays them AUDPRC -> AUDCODEC DAC ->
  NS4150B amplifier (PA20 high only while playing), in its own thread.
- I2C (`i2c scan` on the repeater/room server CLI; `SF32_I2C_SCAN=1 ./build.sh ...` logs
  it at boot): i2c1 has 0x03 SGM41562B charger, 0x28 BHI260AP IMU (Bosch sensor hub:
  accelerometer + gyroscope; needs its firmware uploaded at boot, LilyGo's SDK has a
  driver), 0x5a CST9220 touch. i2c2 and i2c4 are empty. No vibration motor on this board
  (that's on the T-SF32-Keyboard).
- `/sf32_boot` records why the board last stopped: a deliberate power-off writes its
  reason (menu, battery empty, command), a reboot "reboot"; "running" left over at the
  next start means power was lost or the chip reset. Events (starts with the previous
  stop, power-offs, reboots, battery warnings) go to `/sf32_log` (last 20), shown in
  **Settings > Logs**. Flashing looks like "power lost or crash" (the flasher resets the chip).
  Each entry also stores a snapshot of the board when it was written (after a 0x1F byte:
  uptime, battery mV, USB/charge state, chip temperature, free heap and its low point, CPU
  MHz; starts add the power-on mode and PMU wake status). Tap an entry in Logs to see it;
  tap or swipe to go back. 20 entries is about 3 KB; a future SD card could keep more.

## Power saving, battery warnings, burn-in

- The CPU sleeps between tasks (idle hook, WFE: the SDK's own idle instruction; WFI
  missed the BLE controller's wake-up events and broke Bluetooth). MeshCore's loop runs
  every 4 ms instead of 1 ms.
- Bluetooth advertises every 30 ms for 30 s after boot / a disconnect, then every 500 ms.
- The board asks a newly connected phone to pair only if it doesn't start pairing itself
  within 1.5 s (an earlier request collided with the phone's own and dropped the link).
- Battery warnings (companion): a popup at 20 %, at 10 % it also wakes the screen; a
  "Battery empty" screen before the automatic power-off (below). Re-armed when charging.
- Burn-in: the whole layout moves by up to 3 pixels every 3 minutes. With Timeout
  "never (USB)" the screen dims to 10 % and shows the clock after a minute without input;
  the first touch only brightens it.

## Battery protection

On battery (no USB) below 3550 mV for 30 s, the board powers off (SGM41562B shipping mode,
same threshold as LilyGo's factory firmware). `poweroff` / `shutdown` do the same. The
voltage is a rolling average of one ADC reading every 2 s. If the charger cycles, as it
does with no battery, the board reports 0 V instead of a meaningless value.

## Layout

    project*/     SiFli/SCons projects, one per firmware (proj.conf = Kconfig options;
                  project_companion adds Bluetooth)
    src/main.cpp  runs MeshCore's setup()/loop() as the RT-Thread main thread
    src/platform/ T-Display-SF32 specifics: SF32Board (battery, charger, watchdog,
                  power-off, CLI extras), SF32SX1262Radio (mesh::Radio), SF32Display
                  (AMOLED), SF32TouchInput, SF32SerialBLEInterface (+_events.c),
                  SF32RTCClock, SF32HardwareRNG, target.h/.cpp, variant.h (defaults),
                  compat/ (companion-only header shims)
    src/arduino/  minimal Arduino layer: Arduino.h (+Serial, GPIO), Stream.h,
                  FS.h (littlefs + directories), RTClib.h, LittleFS.h, sf32_platform.h
    src/companion_ui/ the companion's screen UI: UITask.cpp is MeshCore's ui-new (changes
                  marked "SF32"); the board's screens, menus, settings and power
                  features are in ui_*.inc, #included by it (see its header)
    src/selftest/ bring-up self-tests
    lib/          vendored Crypto, CayenneLPP, base64, and MeshCore (lib/MeshCore,
                  fetched by setup.sh, unmodified). See lib/README.md.
    tools/        sf32_debug_uart.py: flashing helper and crash debugging (see below);
                  shot.sh: screenshots; gen_font.py: the UI font
    sdk-patches/  the SDK fixes below (setup.sh applies them)
    setup.sh      one-time setup: MeshCore, SDK fixes, SDK link
    env.sh        activates the SDK through ~/sifli-lilygo-sdk (a symlink; the SDK's
                  linker flags break on paths with spaces). SF32_SDK_DIR picks the SDK.
    licenses/, THIRD_PARTY.md   licenses of the bundled code and font

## Fixes this port needs in the LilyGo SDK (sdk-patches/, applied by setup.sh)

- `rtos/env/packages/` copied from the upstream OpenSiFli SDK (missing from the LilyGo
  download).
- Added placeholder Kconfig files at `t_sf32_display_hw_device/sensor/bme280/` and
  `t_sf32_display_hw_device/uart_mux/gps/lwgps2rtt/`. LilyGo's Kconfig points to them
  but their repo doesn't contain them.
- `rtos/rtthread/bsp/sifli/drivers/drv_adc.c`: the two unconditional `rt_kprintf`s in the
  ADC read paths (`ch[7]voltage=...`) now need `DRV_ADC_DEBUG_PRINT`. They printed on every
  battery read and polluted the CLI/companion console.

## How the build fits together

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

## Flashing and debugging

sftool always connects at 1 Mbaud through the SF32's hardware debug-over-UART channel,
which listens at whatever baud UART1 runs at (115200 in this firmware). `flash.sh` first
runs `tools/sf32_debug_uart.py download`. That enters debug mode at the current baud,
halts the CPU (MeshCore's CLI would echo sftool's frames back), stops the hardware
watchdogs, and switches UART1 to 1 Mbaud for sftool.

The same channel works on a running or crashed board:
- `tools/sf32_debug_uart.py PORT where`: halt, print PC/LR/SP/MSP/PSP, resume.
- `tools/sf32_debug_uart.py PORT mem ADDR N`: read memory, e.g. the fault registers
  at E000ED28.
- `tools/sf32_debug_uart.py PORT shot ADDR out.png`: screenshot (see Display).
- Never do block reads of XIP flash (0x1xxxxxxx) over this channel: the chip hangs
  until the watchdog reboots it.

Use `arm-none-eabi-addr2line -e <build>/main.elf` on the addresses. A PC stuck in
`rt_hw_hard_fault_exception` means a HardFault. The frame is on the PSP after the
handler's saved registers.

## Notes

- Console: uart1 at 115200 via the CH343P, msh disabled, logs filtered (warnings for
  repeater/room server, none for the companion, whose console carries binary frames).
  LilyGo's LCD driver still prints `CO5300_ReadID 0x0` when the panel powers on.
- The host can't reset the board: DTR/RTS do nothing. It boots right after flashing.
- LilyGo C RadioLib quirks worked around in SF32SX1262Radio.cpp:
  - Its SPI layer waits for BUSY low *before* asserting NSS, so it can't wake a sleeping
    SX1262. `wakeFromSleep()` pulses NSS.
  - `SX126x_scanChannel()` issues SetCAD from RX; the chip only accepts it in standby.
  - `SX126x_startReceive()` doesn't latch PREAMBLE_DETECTED/HEADER_VALID, which MeshCore
    needs, so the wrapper has its own receive start.
  - SNR is kept in whole dB; the wrapper reads packet status for 0.25 dB.
- Bluetooth notes (SF32SerialBLEInterface.cpp):
  - The device sends a security request on connect (phones then ask for the PIN).
  - Security level 3 forbids unauthenticated pairing.
  - The stack reports bonded re-encryption as auth 0x09 (no MITM bit), so "connected"
    means encrypted with Secure Connections.
- The WFI idle hook (CPU sleeps when idle) is on for repeater/room server but **off in the
  companion**: it broke HCPU <-> LCPU (BLE controller) traffic, and connections failed.
- Watchdog: fed from MeshCore's loop. It reboots about 1 min after a stall (two stages).
- RNG: identities mix the SF32 hardware TRNG with SX1262 wideband noise.
- Clock: on-chip RTC (32.768 kHz crystal), kept across reboots and the menu's power off
  (PMU shutdown; button A turns the board back on). Lost on a battery-empty shutdown
  (shipping mode), with the battery switch off, and on flashing; the clock page then
  shows "--:--" until the phone sets the time (MeshCore's guess from contact timestamps
  isn't shown). The companion's `bootstrapRTCfromContacts()` sets the clock to the
  newest contact's timestamp at every boot, which threw a synced clock back to May 2024,
  so SF32RTCClock never moves a synced (2025+) clock backwards, like MeshCore's own time
  commands. `clock reset` still does.
- Storage: littlefs on the 4 MB `FS_REGION` partition (0x128A0000), formatted on first boot.
- Main thread stack is 16 KB (the SDK's 2 KB default overflows in Ed25519).
- Crypto (192 MHz): Ed25519 sign 4 ms, verify 47 ms, ECDH 7 ms.
- The SDK compiles with `-funsigned-char -fshort-enums`.

## Measurements and open issues

- Noise floor: about -106 dBm with the battery on (-103 with it off) vs -115 on a
  T1000-E nearby. It's broadband and unaffected by the SX1262 regulator mode. The bright
  display adds ~2 dB. There's a spur at 912 MHz (19 x 48 MHz system clock), 1.5 MHz from
  the US preset.
- Battery ADC clips at about 4.3 V (VBAT/2 into the ADC's range), so a full cell reads ~4.28 V.
- To do / ideas:
  - Power: measure the current draw; the SDK's full power manager (light/deep sleep)
    and MeshCore's sleep mode (`board.sleep()` is a no-op) could save more.
  - BHI260AP: auto-rotate the screen; a fall / no-motion safety alert.
  - A USB companion build (MeshCore protocol over USB instead of Bluetooth).
  - Touch on the repeater/room server screens; buttons B and C.
  - Observer / WiFi (needs the T-SF32-Keyboard expansion's ESP32-C6, per LilyGo).
  - Time zone from the phone (needs MeshCore companion protocol support).
