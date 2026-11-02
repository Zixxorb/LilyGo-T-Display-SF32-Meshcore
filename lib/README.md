# Vendored libraries

| Library | Version | Source | Contents |
|---|---|---|---|
| Crypto | 0.4.0 | rweather/Crypto (PlatformIO) | Only what MeshCore uses: SHA256, SHA512, AES128, Ed25519 (verify), plus their base classes and `utility/`. |
| CayenneLPP | 1.6.1 | electroniccats/CayenneLPP (PlatformIO) | `src/` and the license. |
| base64 | 1.4.0 (commit ac168f5) | Densaugeo/base64_arduino (GitHub) | `base64.hpp` (header-only), license. Used by BaseChatMesh (companion). |
| MeshCore | v1.18.0 (developed against) | meshcore-dev/MeshCore, fetched by `../setup.sh` | Used unmodified. Includes orlp/ed25519 in `MeshCore/lib/ed25519`. |

Versions match the ones pinned in MeshCore's `platformio.ini`.

## Local changes

- `CayenneLPP/CayenneLPPPolyline.cpp`, `writeHeader()`: `std::max(m_buffer.size(), 8UL)`
  becomes `std::max<size_t>(m_buffer.size(), 8)`. The original only compiles where
  `size_t` is `unsigned long`. It fails on 32-bit ARM, where `size_t` is `unsigned int`.
