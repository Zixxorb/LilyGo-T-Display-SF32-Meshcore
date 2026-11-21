// Force-included (-include) into every T-Deck C++ file, like the SF32's sf32_platform.h: the
// filesystem the shared SF32 code calls InternalFS (the T-Deck's SPIFFS data partition).
#pragma once
#if defined(__cplusplus) && __has_include(<SPIFFS.h>)  // (libraries that can't see it: not needed)
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#define InternalFS SPIFFS
#endif
