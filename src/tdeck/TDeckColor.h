// The T-Deck's colour correction: Vesper's colours were chosen on the SF32's AMOLED (true black,
// vivid); on the T-Deck's backlit IPS LCD they look flat. Every pixel sent to the panel goes
// through a table (TDeckPanel.cpp) that adds saturation and a little contrast, keeping black and
// white where they are. The frame buffers (and screenshots) keep the UI's own colours.
#pragma once
#include <stdint.h>

#define TDECK_COLOR_PRESETS 5
void tdeckColorSet(int preset);           // 0 none .. 4 (see TDeckColor.cpp)
int tdeckColorPreset();
extern const uint16_t* tdeck_color_lut;   // RGB565 in -> panel order (byte-swapped) out
