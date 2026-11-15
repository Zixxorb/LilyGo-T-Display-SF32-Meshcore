<div align="center">

# Vesper

**VesperOS: MeshCore firmware for the LilyGo T-Display-SF32, a pocket node with a 480x480 AMOLED touch screen.**

Companion (phone app over Bluetooth, with its own touch UI) · Repeater · Room server

![MeshCore](https://img.shields.io/badge/MeshCore-v1.18-1f2937)
![Board](https://img.shields.io/badge/board-T--Display--SF32-3b82f6)
![Radio](https://img.shields.io/badge/radio-SX1262%20915%20MHz-22c55e)
![License](https://img.shields.io/badge/license-MIT-lightgrey)

<img src="docs/images/face-color.png" width="200"> <img src="docs/images/face-sun.png" width="200"> <img src="docs/images/face-info.png" width="200">

</div>

**Vesper** (VesperOS) is a custom firmware for the board with MeshCore running underneath,
unmodified, so it works with the official MeshCore apps and the rest of the mesh. It's built
on LilyGo's SiFli SDK and C radio library. Vesper has its own version number (shown on the
start screen and in About), separate from the MeshCore version it runs.

The companion is a **handheld node**, not a messenger: the phone does the messaging, the
device shows what's happening on the mesh, tests links, finds repeaters and keeps time.

## Contents

<div align="center">

| Start here | More detail |
|---|---|
| [Highlights](#highlights) | [Features in detail](#features-in-detail) |
| [Screenshots](#screenshots) | [Battery and power](#battery-and-power) |
| [Firmwares](#firmwares) | [Repeater and room server](#repeater-and-room-server) |
| [Flashing without building](#flashing-without-building) | [Status and to-do](#status-and-to-do) |
| [Building from source](#building-from-source) | [Development](#development) |
| [Using the companion](#using-the-companion) | [License and credits](#license-and-credits) |

</div>

## Highlights

| Feature | What it does |
|---|---|
| **6 clock faces** | Color, Sun (sunrise/sunset arc), Digital, Analog, Big, Info. Hold the clock to switch |
| **Signal tools** | Signal tester, repeater **finder** (warmer/colder), 902-928 MHz **band scan** |
| **Mesh at a glance** | Activity graph, heard nodes, **My Nodes** with on-demand battery |
| **Battery** | 48 h history graph, time left, low-battery protection |
| **Mesh SOS** | Triple-press A: location + battery to a channel, repeated |
| **Extras** | Flashlight (white/red), share contact QR, auto adverts, mesh time, rotation |
| **USB app** | MeshCore app protocol over USB too: PC tools and a raw-packet **observer** |
| **Themes** | Normal or frosted Glass style, 9 colours plus Rainbow on each, night mode, always-on display, tap sounds |

## Screenshots

<table>
<tr>
<td align="center"><img src="docs/images/face-color.png" width="180"><br><sub>Color face</sub></td>
<td align="center"><img src="docs/images/face-sun.png" width="180"><br><sub>Sun face</sub></td>
<td align="center"><img src="docs/images/face-analog.png" width="180"><br><sub>Analog face</sub></td>
<td align="center"><img src="docs/images/face-big.png" width="180"><br><sub>Big face</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/images/battery.png" width="180"><br><sub>Battery history</sub></td>
<td align="center"><img src="docs/images/radio.png" width="180"><br><sub>Radio</sub></td>
<td align="center"><img src="docs/images/bandscan.png" width="180"><br><sub>Band scan</sub></td>
<td align="center"><img src="docs/images/nodes.png" width="180"><br><sub>My Nodes</sub></td>
</tr>
<tr>
<td align="center"><img src="docs/images/face-info.png" width="180"><br><sub>Info face</sub></td>
<td align="center"><img src="docs/images/settings.png" width="180"><br><sub>Settings</sub></td>
<td align="center"><img src="docs/images/display.png" width="180"><br><sub>Display settings</sub></td>
<td align="center"><img src="docs/images/powermenu.png" width="180"><br><sub>Power menu (swipe down)</sub></td>
</tr>
</table>

## Firmwares

| Firmware | Use it for |
|---|---|
| **Companion** | Your own node: pairs with the MeshCore phone app over Bluetooth, full touch UI |
| **Repeater** | Extending the mesh. Configure with [config.meshcore.io](https://config.meshcore.io) |
| **Room server** | A shared message board on the mesh |

All three share the node identity and settings on the board's flash, which survive
reflashing, so you can switch between them.

> [!IMPORTANT]
> **Battery switch:** the red I/O switch on the board disconnects the battery. On **O** the
> board only runs on USB, loses its clock on unplug, and the charger cycles with nothing
> attached. Keep it on **I**.

## Flashing without building

No SDK or compiler needed: Python 3 and a USB cable.

1. Install pyserial:
   - Windows: `py -m pip install pyserial`
   - macOS: `python3 -m pip install pyserial`
   - Debian/Ubuntu: `sudo apt install python3-serial`
   - Fedora: `sudo dnf install python3-pyserial`
   - Arch/CachyOS: `sudo pacman -S python-pyserial`

   If pip refuses with "externally-managed-environment", use a virtual environment:
   `python3 -m venv ~/sf32-venv && ~/sf32-venv/bin/pip install pyserial`, then run the
   flasher with `~/sf32-venv/bin/python sf32-flash.py` instead of `python3 sf32-flash.py`.
2. Download [`flasher/sf32-flash.py`](flasher/sf32-flash.py) and a firmware zip
   (`companion.zip`, `repeater.zip` or `room_server.zip`) from the
   [Releases](../../releases) page (or let the flasher download the latest release).
3. Plug in the board and run:
   ```sh
   python3 sf32-flash.py companion.zip
   ```

It finds the board, downloads SiFli's flashing tool (`sftool`, checked against a pinned
checksum) on the first run, and flashes. Your settings, contacts and node identity are
kept. Run it with no arguments to pick a firmware from a menu (downloaded from this repo's
latest release). Flashing a real board is tested on Linux only. On Windows and macOS, automated
checks cover everything except the flash itself (Windows may need the CH343 USB driver).

## Building from source

You need Linux or macOS with bash or zsh, git, and the board on USB.

1. Get the [LilyGo SiFli SDK](https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo) v2.5.1
   (folder `sifli-sdk-v2.5.1`) and install its toolchain: run `./install.sh` in that folder
   (it needs [uv](https://docs.astral.sh/uv/)).
2. Clone the upstream [SiFli SDK](https://github.com/OpenSiFli/SiFli-SDK) too: LilyGo's
   download lacks `rtos/env/packages`.
3. Run setup. It fetches MeshCore into `lib/MeshCore` (the tested version,
   `companion-v1.18.0`; pick another with `MESHCORE_REF=<tag>`), applies the SDK fixes in
   `sdk-patches/`, and links the SDK at `~/sifli-lilygo-sdk` (its build breaks on paths
   with spaces):
   ```sh
   ./setup.sh /path/to/sifli-sdk-v2.5.1 /path/to/SiFli-SDK
   ```
4. Build and flash:
   ```sh
   ./build.sh companion && ./flash.sh companion
   ```

Everyday commands:

```sh
./build.sh [repeater|room_server|companion]   # default: repeater
./flash.sh [repeater|room_server|companion]   # flash over USB (auto-picks the port)
./monitor.sh                                  # serial console at 115200; Ctrl+] to quit
```

> [!TIP]
> **Serial port permission on Linux:** add yourself to the `uucp`/`dialout` group, or add a
> udev rule such as `SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", MODE="0666"`.
> From fish, run the scripts as normal; they start bash themselves.

## Using the companion

### Pairing

The board advertises as `MeshCore-<name>`. On first connect the phone asks for a PIN: it's
the 6-digit **Pin** shown on the screen (new each boot until a phone is paired). Pairing
uses LE Secure Connections with bonding, so later connections need no PIN.

> If a phone was paired with older test firmware, remove the device from the phone's
> Bluetooth settings first.

### Gestures

| Gesture | Does |
|---|---|
| **Swipe left / right** | Next / previous page |
| **Tap** or button **A** | Next page (double tap: previous; long press: select) |
| **Swipe down** | Power menu: Flashlight, Screen off, Low power, Send SOS, Reboot / off |
| **Swipe up** | The current page's menu, plus **Settings** |
| **Swipe right** in Settings | Back one level |
| **Hold** the clock | Next clock face (a ring fills; at 0.6 s it changes with a pop) |
| **Triple-press A** | Mesh SOS |

Button **A** wakes a dark screen (taps too, with Settings > Display > **Tap to wake** on). Woken within 10 s, it comes back where it was; after that,
on the clock. Long lists follow your finger and glide after a flick.

### Pages

<details>
<summary><b>All 13 pages</b></summary>

| Page | Shows |
|---|---|
| **Clock** | Time, date, unread envelope (tap to read), battery. Six faces |
| **Status** | Unread, messages waiting for the app, Bluetooth PIN or node name |
| **Battery** | Charge, voltage, chip temperature, time left, 48 h graph |
| **Heard** | Recently heard nodes |
| **Radio** | Frequency, BW, SF, CR, TX power, noise floor. Menu: edit radio, band scan |
| **Bluetooth** | Connection; menu: on/off, **Pair phone** (a new phone can pair only for 2 min after this, or in the first 5 min after start while none is paired; one wrong PIN closes it), forget paired phones |
| **Advert** | Send an advert (hold the icon or long press button A: nearby or whole mesh, your choice) |
| **Bot** | Optional auto-reply to senders using 1-byte path hashes (off by default) |
| **Discover** | Scan for nearby nodes |
| **Signal** | Signal tester and repeater finder |
| **Activity** | Packets per hour over 24 h and how busy the channel is |
| **Nodes** | Your favourite contacts: last heard, hops, battery on tap |
| **Power** | Hold the icon for the power menu |

Pages can be hidden in **Settings > Pages** (the clock always shows).

</details>

### Settings

<details>
<summary><b>All the menus</b></summary>

| Menu | Contains |
|---|---|
| **Theme** | Style (Normal / Glass), Color (each style keeps its own), Backdrop for Glass (Aurora / Dunes / Blend / None), Battery (Color: red / yellow / green by charge, or Plain) |
| **Display** | Brightness, timeout, always-on, tap to wake, rotation (Normal / Auto / Right / Flipped / Left) |
| **Pages** | Show or hide each home page |
| **Clock** | Face, zone, daylight saving (off/US/EU), 12/24 h, mesh time, reset clock |
| **Battery** | Show (icon + % / % only), graph span (6/12/24/48 h), low alert (10/15/20/30 % / off), charge to (100/90/80 %) |
| **Sound** | Volume, new-message alerts (all / direct / off), taps (click / pop / tick / off) |
| **Night** | From, until, dim and how far, do not disturb |
| **Motion** | Lift to wake, flip to quiet |
| **Bluetooth** | On / off (remembered) |
| **USB app** | The app protocol over USB as well (off by default) |
| **Adverts** | Nearby every 1/3/6/12 h, mesh every 12/24/48 h (both off by default), what the Advert page's hold sends |
| **Bot** | Auto-reply, to DMs, channels, daily max (5/10/20/50), repeat per sender (12 h / 24 h / 7 d), gap between replies (1/5/15 min) |
| **Low power** | Mode on / off, and Auto at (off / 10 / 15 / 20 / 30 % battery): see [Battery and power](#battery-and-power) |
| **SOS to** | The channel Mesh SOS goes to (#emergency by default) |
| **Sleep** | Let the chip sleep with the screen off (on by default) |
| **Logs** | The last 20 events; tap one for a board snapshot |
| **Power** | Flashlight, screen off, low power on / off, Send SOS, Reboot / off (reboot, power off) |

A page's swipe-up menu has that page's actions, and a "... menu" row for its settings
(the same submenu as in Settings; Back returns to where you came from).

Saved in `/sf32_ui` on the board.

</details>

## Features in detail

Click a feature to expand it.

### Clock and time

<details>
<summary><b>Clock faces</b></summary>

- **Color**: a rainbow ring of minute dots filling up through the hour, a colour per digit, on true black.
- **Sun**: the day as an arc from sunrise to sunset with the sun on it, a pale moon below
  at night, the times and how long until the next. Worked out on the device from the
  node's location as set in the app (the sunrise equation; no network).
- **Digital**, **Analog**, **Big** (stacked hours and minutes), **Info** (time plus unread,
  battery, phone and last-heard cards; also shows the next sunrise or sunset).

Switch by holding the clock, or Settings > Clock > Face (also via the clock page's menu > Clock menu).
The time comes from the phone app when it connects; until then the clock shows `--:--`.

</details>

<details>
<summary><b>Time zone</b></summary>

Settings > Clock > Zone: **-** / **+**, **Done** (button A: click = +1 h, double
click = -1 h, long press = done). The phone can't set it: MeshCore's companion protocol only
sends UTC. With **Daylight saving** set to US or EU, the zone is your standard time (e.g.
UTC-5 for US Eastern) and summer time is added automatically. It's MeshCore's own
`tz_offset` setting (also `set tz.offset N` via the app's CLI).

</details>

<details>
<summary><b>Mesh time</b></summary>

While the clock isn't known (battery ran flat, battery switch, fresh flash), the times in
received adverts set it once 3 different nodes agree within 5 minutes. The clock shows
"(mesh)" after the date until the phone syncs. Only nodes already in your contacts count;
outliers are ignored. **Reset clock** (Settings > Clock, tap twice) sets the clock back to
"unknown", the way out of a wrong time ahead (MeshCore only moves the clock forward).

</details>

### Messages and safety

<details>
<summary><b>Messages</b></summary>

The device shows messages but doesn't send them: no typing or replies, by design. A new
message that alerts (Settings > Sound > Alerts) shows a banner at the top for 6 s with the
sender and the start of the text. Tap the envelope on the clock to read; swipe left for the
next, right to go back. Messages count as read once the phone app downloads them, or when
viewed on the device. **Do not disturb** (Settings > Night) keeps them silent and dark.

</details>

<details>
<summary><b>Mesh SOS</b></summary>

Triple-press button A, or swipe down > Send SOS. After a 5 s countdown with beeps (Cancel,
or press A), it sends "SOS! I need help. Location: lat,lon. Battery N%." to the channel in
Settings > SOS to (default: **#emergency**, the shared hashtag channel, added to the channel
list when an SOS needs it; a chosen channel that has gone falls back to it), and repeats every 5 min, 6
times in all, until you tap Stop. The location is the node's as set in the app.

</details>

### Radio and mesh tools

<details>
<summary><b>Signal tester and repeater finder</b></summary>

**Signal tester** (Signal page, hold the icon or the results to start/stop): every 10/15/30/60 s it pings the
repeaters in direct range (zero-hop discovery) and shows how well the best one hears us and
we hear it (SNR), its name, replies/pings and a graph of the last 40 pings (no reply: red
tick). A beep per ping, higher for a better link. Swipe up > Target locks onto the repeater
just heard. The screen stays on while testing; it stops by itself after 30 min.

**Finder** (Signal menu): locked onto one repeater, a ping every 5 s, its signal strength
big (RSSI, which keeps rising as you get close) and **warmer / colder / about the same**
against the last few pings, the best so far, and a beep whose pitch follows the strength.
No compass on the board, so no direction: walk, and follow the trend.

</details>

<details>
<summary><b>Radio editor and band scan</b></summary>

**Edit radio** (Radio page menu): one card per setting (Preset, Frequency, BW, SF, CR, TX
power). Tap one for its own screen with saved vs new value, **-** / **+** (frequency has a
step size and is limited to 902-928 MHz) and **Done**. Changes are tinted amber; **Apply N
changes** lists them (old -> new) to confirm. Nothing reaches the radio until then.
Presets: MeshCore's USA/Canada, Australia and New Zealand (narrow).

**Band scan** (Radio page menu): the noise level across 902-928 MHz in 250 kHz steps, swept
continuously, with max-hold marks, our channel marked, and the strongest frequency below.
MeshCore keeps receiving while it runs. Tap to reset.

</details>

<details>
<summary><b>Activity</b></summary>

Packets heard in the last hour and 24 h, how busy the channel is (share of the time the
radio spent receiving or sending) and packets per hour as a bar graph. From the radio's own
counters: no extra traffic. RAM only (starts over at boot).

</details>

<details>
<summary><b>My Nodes</b></summary>

The contacts you starred as favourites in the app (up to 4): when each was last heard,
direct / hops / flood, and its battery **when you tap its row**: one telemetry request to
that node, at most one every 20 s and nothing on a schedule, so the mesh isn't loaded with
polling. A node answers only if its own telemetry setting shares with you; repeaters want a
login for it ("no reply").

</details>

<details>
<summary><b>Auto advert</b></summary>

Send adverts on a schedule, as repeaters do, so other nodes keep a fresh path: **Nearby**
(zero hop) every 1, 3, 6 or 12 h and **Mesh** (flood) every 12, 24 or 48 h. The Advert,
Heard and Radio page menus have **Send nearby** and **Send to mesh**; the Advert page's
menu > Advert menu > **Hold** picks what its icon hold sends.

</details>

### Everything else

<details>
<summary><b>Share QR, flashlight, rotation</b></summary>

- **Share QR** (Clock or Status page menu): this node as a QR code for the MeshCore app to
  scan (`meshcore://contact/add?...`).
- **Flashlight** (power menu): the whole panel at full brightness, white or red (tap
  switches; red keeps night vision), 5 min unless touched.
- **Rotation** (Settings > Display): Normal, Right, Flipped, Left, or **Auto** (the way
  it's held, from the accelerometer; lying flat keeps the last). Touches turn with it.

</details>

<details>
<summary><b>Display, sound and motion options</b></summary>

- **Glass style** (Settings > Theme): a wallpaper in the theme's colours (Aurora, Dunes or
  Blend) or plain black (None), with cards as frosted panes and popups that blur what's behind them. It lights more of the screen than Normal, so it uses more power while the
  screen is on. The always-on clock stays on black.
- **Themes**: MeshCore blue, cyan, teal, green, purple, pink, red, orange, mono, and
  Rainbow (every page in its own hue).
- **Tap to wake** (Settings > Display, Off by default): taps wake a dark screen. Off, the
  touch controller sleeps while the screen is dark and only button A wakes it: in a night
  test that cut the screen-off battery drain by about 60%.
- **Always-on**: instead of going dark, the time, date and an unread envelope on black at
  6 % brightness, moved each minute (burn-in care).
- **Night** (Settings > Night): a lower brightness between two hours and/or do not disturb. There's no
  light sensor for auto brightness.
- **Timeout**: 15 s, 30 s, 1 min, 5 min, or never while on USB (then it dims after a minute).
- **Sound**: chime volume off / low / medium / high; tap sounds click / pop / tick plus a
  whoosh on swipes. No vibration motor on this board.
- **Lift to wake**: lying still, then tilted, turns the screen on (walking or a pocket
  doesn't). **Flip to quiet**: face down turns the screen off and silences alerts.

</details>

<details>
<summary><b>USB app (PC tools and observer)</b></summary>

Settings > USB app (off by default): the MeshCore app protocol over the USB serial port as
well as Bluetooth, both at once. Use it with PC apps (meshcore-cli, the Python library) or
as an **observer**: every packet heard is pushed raw with its SNR / RSSI
(`PUSH_CODE_LOG_RX_DATA`), for logging or an MQTT bridge on the PC. 115200 baud on the
CH343 port; flashing still works. Nothing is written until an app sends a frame.

</details>

<details>
<summary><b>Bot (optional)</b></summary>

Off by default; tap the Bot page to turn it on. It auto-replies to messages sent with
**1-byte path hashes**, asking the sender to switch to 2-byte. Settings > Bot (or the
page's menu > Bot menu): **Auto-reply**, **To DMs**, and **Channels** (off / DM / post in
the channel), and its limits: by default each sender gets it at most once a day (**Repeat**),
at most one reply a minute (**Gap**) and 20 a day in all (**Daily max**); never to
auto-replies.

</details>

## Battery and power

- **Battery life** (companion, screen mostly off): an estimated **28-30 hours** on the
  480 mAh battery (about 29 mV per hour overnight, roughly 16-17 mA). With the screen off,
  the touch controller sleeps (unless Settings > Display > Tap to wake is on) and the
  audio chip powers down; the LoRa radio keeps listening all the time (about 5 mA of that).
- **Low power mode** (Power menu, or Settings > Low power): for when only the mesh matters,
  say someone lost. The clock page becomes a minimal one (dimmer time on black, "Low power"
  under the date), and it turns off tap and swipe sounds (message chimes stay), the
  auto-reply bot, the motion sensor (button A wakes the screen), the always-on clock and the
  Glass style; Bluetooth advertises every 2 s instead of 500 ms while no phone is connected.
  The radio keeps listening and your phone can still connect. Your settings aren't changed:
  turning it off puts everything back. **Auto at** turns it on by itself when the battery
  reaches that level (once per discharge, not again if you turn it off), and USB power turns
  an automatic one back off. It saves power while the screen is on; with the screen off the
  board is already about as lean as it gets.
- **Sleep** (on by default): the chip light-sleeps whenever the screen is off; the
  radio keeps listening and Bluetooth stays up. It stays awake on USB (charging), which
  keeps flashing working.
- **Charge shown**: from the battery voltage. While charging (which lifts the voltage) it
  carries on from the reading before USB at the charger's pace, and shows 100 % only when
  the charger reports done.
- **Warnings**: a popup at 20 %; at 10 % it also wakes the screen.
- **Protection**: on battery below 3.55 V for 30 s the board powers off (same threshold as
  LilyGo's factory firmware).
- **Clock**: kept across reboots and the menu's power off; lost on a battery-empty
  shutdown, with the battery switch off, and on flashing.

## Repeater and room server

Configure them with [config.meshcore.io](https://config.meshcore.io) (Chrome/Edge, USB, the
CH343P port): the board speaks MeshCore's stock serial CLI at 115200 baud. Over LoRa, use
Remote Management in the MeshCore app.

| Default | Value |
|---|---|
| Radio | USA/Canada preset: 910.525 MHz, BW 62.5, SF7, CR 4/5, 22 dBm |
| Names | Repeater "SF32 Repeater", room server "SF32 Room" (room password "hello") |
| Admin password | **"password"**: change it |

The screen shows MeshCore's repeater/room screen and turns off after 20 s. Button **A**
turns it on; holding A for 1 s powers off. To power on, press **D** (the charger's button
on the bottom edge, below the antenna) or plug in USB. The touchscreen isn't used here: its
controller is put to sleep at boot, which saves battery.

<details>
<summary><b>Board-specific CLI commands</b></summary>

"USB only" means typed at the USB console: the phone app, the USB app and remote CLI can't run them.

| Command | Does |
|---|---|
| `battery` | Battery voltage over 20 samples, USB, charger state and faults |
| `charger` | Raw SGM41562B registers 00-0B, plus HIZ / CEB / FET_DIS |
| `charger test` | (USB only) Pauses charging for 3 s and reads VBAT (0 V = no battery) |
| `clock reset` | (USB only) RTC back to MeshCore's default start date |
| `buttons` | Levels and change counts for buttons C, A, B |
| `display on` / `display off` | Screen |
| `noise` / `noise test` / `noise scan FROM TO STEP` | Radio background level, tests and sweeps (kHz; USB only) |
| `i2c scan` | (USB only) Devices on the I2C buses |
| `wdt test` | (USB only) Stalls the main loop; the watchdog reboots in about a minute |
</details>

## Status and to-do

Working: all three firmwares, Bluetooth pairing, touch UI, sleep, sound, motion, USB app.

- **Power**: about 28-30 hours with the screen mostly off (roughly 16-17 mA on the rated
  480 mAh), up from about 22. The touch controller sleeps and the audio chip powers down
  while the screen is off; the radio keeps listening all the time (about 5 mA). See
  [the measurements](docs/DEVELOPMENT.md#measurements).
- **WiFi**: needs the T-SF32-Keyboard expansion's ESP32-C6 (per LilyGo).
- Noise floor reads about -103 to -106 dBm vs -115 on a T1000-E nearby (broadband; a spur at
  912 MHz from the board's own clock).

## Development

Internals, build details, SDK patches, debugging over the debug UART, screenshots and test
builds are in **[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)**.

```
project*/         one SiFli/SCons project per firmware
src/main.cpp      runs MeshCore's setup()/loop() as the RT-Thread main thread
src/platform/     board support: battery, radio, display, touch, BLE, USB, RTC, motion, sound
src/arduino/      a minimal Arduino layer for MeshCore
src/companion_ui/ the companion's screen UI (MeshCore's ui-new plus ui_*.inc)
lib/              vendored libraries; MeshCore is fetched by setup.sh, unmodified
tools/            flashing helper, screenshots, remote touch, battery counters, font generator
flasher/          stand-alone flasher for users (no SDK needed)
ci/               build-all.sh: the clean build GitHub Actions runs on code pushes
sdk-patches/      fixes the LilyGo SDK needs (applied by setup.sh)
```

## License and credits

Vesper is MIT, see [LICENSE](LICENSE). Bundled code and the font keep their own licenses: see
[THIRD_PARTY.md](THIRD_PARTY.md) and `licenses/`. Built on [MeshCore](https://github.com/meshcore-dev/MeshCore),
the LilyGo SiFli SDK, Noto Sans (SIL OFL) and Nayuki's QR code generator (MIT).

Developed with help from Claude (Anthropic).
