Vesper (VesperOS): MeshCore (v1.18, unmodified) firmware for the LilyGo T-Display-SF32, built by CI from this tag.

**New in 1.3.0**
- **Ping a repeater**: a repeater's or room server's card has a Ping button. It sends one trace
  there and back and shows the round trip and the signal (SNR) of every hop, both ways.
- **LoRa networks** (Radio page menu): which LoRa networks are on the air around you (MeshCore,
  Meshtastic presets, LoRaWAN), with their signal.
- **Heard repeats and Resend**: the device notices a repeater passing on what you sent ("heard
  2x"), and can resend a channel message nobody repeated (Settings > Resend, off by default).
- **Muted channels** (Settings > Sound > Channels): no sound, no screen wake, no unread count.
- **Messages**: the ~120 most used emoji in colour, curly quotes and accents shown as plain
  text, and a banner with the sender and channel on top.
- **Sounds**: sound themes, message chime choices, more tap styles and event sounds.
- **Starlits meet**: nearby Vesper boards' Starlits visit each other. More looks (mixed parts and
  markings), and tap a need to see what it means.
- **Battery**: Settings > Battery > Usage estimates what used the battery; 0 % is now the point
  where the board powers off (a flat battery used to read 9 %); it won't restart on a flat
  battery; the screen-off loop waits for events instead of polling.
- **Easier to use**: taps never change the page (only swipes do), pages can be reordered,
  Settings is in sections, an SOS runs in the background, RX gain toggle on the Radio page.
- **The Vesper logo** on the boot screen, the real build date, a sun icon and a moon with its
  real phase on the Sun face, and many fixes from a full code review.

**Files**
- `companion.zip`: your own node. Pairs with the MeshCore phone app over Bluetooth and has
  its own touch UI.
- `repeater.zip`: extends the mesh. Configure it at [config.meshcore.io](https://config.meshcore.io).
- `room_server.zip`: a shared message board on the mesh.
- `sf32-flash.py`: the flasher (Python 3 and pyserial). Run `python3 sf32-flash.py` and pick a
  firmware, or pass a zip you downloaded.
- `SHA256SUMS.txt`: checksums of the files above.

The node identity and settings live on the board's flash and survive reflashing, so you can
switch between firmwares.

**Before you flash**
- Keep the red battery switch on **I**: on **O** the board runs only on USB.
- Flashing a real board has been tested on Linux only. On Windows and macOS, automated
  checks cover everything except the flash itself (Windows may need the CH343 USB driver).
- 915 MHz board, US preset (910.525 MHz, 62.5 kHz, SF7, CR5) by default. Change it in the
  phone app (companion) or at config.meshcore.io (repeater, room server).
- Repeater and room server ship with MeshCore's default admin password (`password`; the
  room server's guest password is `hello`). Change them after flashing.

**Battery** (companion): about 28-30 hours on the 480 mAh battery with the screen mostly off;
about a day with regular use. The
touch controller sleeps while the screen is off (button A wakes it; Settings > Display >
Tap to wake brings tap-to-wake back at a higher drain), and the audio chip powers down.

See the [README](https://github.com/Zixxorb/VesperOS#readme) for features, menus and building from source.
