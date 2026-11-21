// Force-included (-include) when compiling MeshCore sources for the SF32.
//
// MeshCore picks its filesystem type per platform in helpers/IdentityStore.h.
// None of those branches match here, so we supply FILESYSTEM ourselves. That
// lets MeshCore compile without modification.
#pragma once

#define SF32_PLATFORM 1

// The day this firmware was built (src/platform/SConscript writes it each build), instead of
// MeshCore's fixed release date: shown on the boot screen, sent to the app, the CLI's "ver"
#ifdef __cplusplus
extern "C" const char sf32_build_date[];
#else
extern const char sf32_build_date[];
#endif
#define FIRMWARE_BUILD_DATE sf32_build_date

#ifdef __cplusplus
#include "Arduino.h"
#include "FS.h"
#define FILESYSTEM fs::FS
#endif
