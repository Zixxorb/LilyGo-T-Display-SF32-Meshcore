# Third-party code and assets

| What | Where | License |
|---|---|---|
| MeshCore (Scott Powell / rippleradios.com and contributors) | fetched into `lib/MeshCore` by `setup.sh`, not in this repo | MIT |
| Companion screen UI, based on MeshCore's `examples/companion_radio/ui-new` | `src/companion_ui/` (changes marked "SF32") | MIT, `licenses/MeshCore-LICENSE.txt` |
| Crypto 0.4.0 (Rhys Weatherley / Southern Storm Software) | `lib/Crypto` (subset) | MIT, in each file's header |
| CayenneLPP 1.6.1 (Electronic Cats) | `lib/CayenneLPP` | MIT, `lib/CayenneLPP/LICENSE.md` |
| base64 1.4.0 (Densaugeo) | `lib/base64` | MIT, `lib/base64/LICENSE` |
| ed25519 (Orson Peters), part of MeshCore | `lib/MeshCore/lib/ed25519` | zlib |
| Arm GNU Toolchain runtime libraries (newlib, libstdc++, libgcc) | not in this repo, linked into the firmware | `licenses/toolchain-license.txt` in the firmware zips |
| Noto Sans Medium (The Noto Project Authors), rendered into glyph bitmaps | `src/platform/SF32Font.h` (made by `tools/gen_font.py`) | SIL OFL 1.1, `licenses/OFL-NotoSans.txt` |
| LilyGo SiFli SDK (with RT-Thread, SiFli HAL, CMSIS, FlashDB, liblc3, mbedTLS, LilyGo's board support and C radio library) | not in this repo (see README), compiled into the firmware | Apache 2.0, `licenses/SiFli-SDK-LICENSE.txt` (LilyGo's readme points to this file for the SDK) |
| BSD-licensed parts of that SDK: littlefs, ST-derived parts of the HAL, board files, RT-Thread's allocator | not in this repo, compiled into the firmware | BSD-3-Clause, `licenses/BSD-3-Clause-notices.txt` |
| SiFli's binary-only libraries in that SDK: Bluetooth stack (`lib_bt_gcc.a`), EPIC (`libhal_epic_ex_gcc.a`) | not in this repo, linked into the firmware | the SDK's license; no separate terms ship with them |
| Bosch BHI260AP driver and sensor firmware image, part of LilyGo's SDK | not in this repo, compiled into the companion | BSD-3-Clause, `licenses/Bosch-BHY2-LICENSE.txt` (Bosch's BHY2_SensorAPI) |

Small changes this port needs in the SDK are in `sdk-patches/`. The firmware zips carry this
file, `LICENSE` and `licenses/`.

## QR Code generator library (src/qrcodegen)

Copyright (c) Project Nayuki. MIT License (the full notice is at the top of
`src/qrcodegen/qrcodegen.h`). https://www.nayuki.io/page/qr-code-generator-library
Copied from the SiFli SDK's LVGL (`external/lvgl_v8/src/extra/libs/qrcode`).
