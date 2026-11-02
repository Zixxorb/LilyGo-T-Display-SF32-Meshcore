// Force-included into MeshCore's companion sources (see lib/SConscript).
//
// The SDK's rtconfig.h defines BLUETOOTH (=1) when Bluetooth is enabled, which
// clashes with the BLUETOOTH enum value in companion_radio/ui-new/UITask.cpp.
// Pull rtconfig.h in first (it's include-guarded) and drop the macro; MeshCore's
// code doesn't use it.
#pragma once

#include <rtconfig.h>
#undef BLUETOOTH
