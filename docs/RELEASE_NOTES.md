MeshCore (v1.18, unmodified) for the LilyGo T-Display-SF32, built by CI from this tag.

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

**Battery** (companion, screen mostly off): about 28-30 hours on the 480 mAh battery. The
touch controller sleeps while the screen is off (button A wakes it; Settings > Display >
Tap to wake brings tap-to-wake back at a higher drain), and the audio chip powers down.

See the [README](https://github.com/Zixxorb/LilyGo-T-Display-SF32-Meshcore#readme) for features, menus and building from source.
