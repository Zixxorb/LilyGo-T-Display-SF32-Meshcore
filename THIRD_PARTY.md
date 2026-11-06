# Third-party code and assets

| What | Where | License |
|---|---|---|
| MeshCore (Scott Powell / rippleradios.com and contributors) | fetched into `lib/MeshCore` by `setup.sh`, not in this repo | MIT |
| Companion screen UI, based on MeshCore's `examples/companion_radio/ui-new` | `src/companion_ui/` (changes marked "SF32") | MIT, `licenses/MeshCore-LICENSE.txt` |
| Crypto 0.4.0 (Rhys Weatherley / Southern Storm Software) | `lib/Crypto` (subset) | MIT, in each file's header |
| CayenneLPP 1.6.1 (Electronic Cats) | `lib/CayenneLPP` | MIT, `lib/CayenneLPP/LICENSE.md` |
| base64 1.4.0 (Densaugeo) | `lib/base64` | MIT, `lib/base64/LICENSE` |
| ed25519 (Orson Peters), part of MeshCore | `lib/MeshCore/lib/ed25519` | zlib |
| Noto Sans Medium (The Noto Project Authors), rendered into glyph bitmaps | `src/platform/SF32Font.h` (made by `tools/gen_font.py`) | SIL OFL 1.1, `licenses/OFL-NotoSans.txt` |
| LilyGo SiFli SDK (with RT-Thread, SiFli HAL, LilyGo's board support and C RadioLib port) | not in this repo (see README), compiled into the firmware | Apache 2.0, `licenses/SiFli-SDK-LICENSE.txt`, and others (see the SDK) |
| Bosch BHI260AP driver and sensor firmware image, part of LilyGo's SDK | not in this repo, compiled into the companion | BSD-3-Clause, `licenses/Bosch-BHY2-LICENSE.txt` (Bosch's BHY2_SensorAPI) |

Small changes this port needs in the SDK are in `sdk-patches/`. The firmware zips carry this
file, `LICENSE` and `licenses/`.

## QR Code generator library (src/qrcodegen)

Copyright (c) Project Nayuki. MIT License (the full notice is at the top of
`src/qrcodegen/qrcodegen.h`). https://www.nayuki.io/page/qr-code-generator-library
Copied from the SiFli SDK's LVGL (`external/lvgl_v8/src/extra/libs/qrcode`).
