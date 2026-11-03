# Versions ci/build-all.sh builds on. GitHub Actions caches the SDKs and toolchain keyed
# on this file: change it and the next run fetches them afresh.
LILYGO_SDK_REPO=https://github.com/Xinyuan-LilyGO/SlFli-SDK-Lilygo
LILYGO_SDK_SHA=036b3649688477df9523f9c7884e77f883feed8f
UPSTREAM_SDK_REPO=https://github.com/OpenSiFli/SiFli-SDK
UPSTREAM_SDK_SHA=e203f6e0cc222204c6af236b9b2d0f33d16f0dc3
MESHCORE_PIN=companion-v1.18.0
