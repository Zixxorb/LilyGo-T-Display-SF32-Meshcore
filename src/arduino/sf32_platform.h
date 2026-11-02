// Force-included (-include) when compiling MeshCore sources for the SF32.
//
// MeshCore picks its filesystem type per platform in helpers/IdentityStore.h.
// None of those branches match here, so we supply FILESYSTEM ourselves. That
// lets MeshCore compile without modification.
#pragma once

#define SF32_PLATFORM 1

#ifdef __cplusplus
#include "Arduino.h"
#include "FS.h"
#define FILESYSTEM fs::FS
#endif
