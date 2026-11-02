// MeshCore's simple_repeater picks its filesystem by platform. We compile those
// files with RP2040_PLATFORM defined (see lib/SConscript), so they take the
// LittleFS branch. Here LittleFS is our littlefs volume on internal flash.
#pragma once

#include "FS.h"

static fs::InternalFlashFS& LittleFS = InternalFS;
