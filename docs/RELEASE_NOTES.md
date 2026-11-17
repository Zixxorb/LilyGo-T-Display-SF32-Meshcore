Vesper (VesperOS): MeshCore (v1.18, unmodified) firmware for the LilyGo T-Display-SF32, built by CI from this tag.

**New in 1.2.0**
- **Starlits**: a little critter of your own, made from your node's key (four kinds, each its own
  shape). It's fed by the mesh traffic your radio hears, and you can stroke it, boop it and play
  with it. It reflects the device: it holds your unread mail (tap it to read), sleeps at night,
  naps in low power mode and looks worried during an SOS. Its line shows **today's finds** (new
  nodes, the farthest packet, signal-tool results). A faint aura shows each node's path-hash size.
- **Friends**: the Starlits of your favourite nodes and the 5 heard most recently. **One card
  per node** (from Friends or the Nodes page): details, path-hash size and battery on request.
- **Mesh health**: the bot marks the nodes it nudged about 1-byte path hashes, and the Bot page
  shows how many nearby nodes are on 2-byte.
- **Low power mode**: minimal clock, slower Bluetooth, power-saving receive gain, 30 fps, a leaf
  icon; on by hand or by itself at a battery level. **An SOS turns it on** (keeping full receive
  sensitivity), and a send that can't go out is retried after 30 s.
- **A Starlit clock face**, a **status row** (USB, low power, night, mail, Bluetooth icons in one
  place), the **ChattaMesh** theme and Glass **tilt light**.
- **Smoother**: About, Logs, Friends and the Starlit draw much faster; no page-turn sound for a
  swipe that turns no page.

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

See the [README](https://github.com/Zixxorb/VesperOS#readme) for features, menus and building from source.
