// Bring-up only (step 2 of the port): the shared display code (src/platform/SF32Display.cpp)
// drawing on the T-Deck through TDeckPanel, before the companion UI runs here: a title bar,
// text in three sizes, the logo, and colour swatches. Replaced by the UI.
#include <Arduino.h>
#include "SF32Display.h"

static SF32Display s_display;

void tdeckTestPattern() {
  if (!s_display.begin()) return;
  s_display.startFrame();
  s_display.setSquareLayout(true);  // 128x128 logical, centred (240x240 panel px)
  s_display.setColor(UIColor::title_bkg);
  s_display.fillRect(0, 0, 128, 14);
  s_display.setColor(UIColor::title_txt);
  s_display.setTextSize(1);
  s_display.drawTextCentered(64, 3, "Vesper on T-Deck");
  int h = s_display.drawLogo(64, 18);
  s_display.setColor(UIColor::primary_txt);
  s_display.drawTextCentered(64, 22 + h, "Hello, mesh!");
  s_display.setTextSize(2);
  s_display.setColor(UIColor::corp_blue);
  s_display.drawTextCentered(64, 34 + h, "12:34");
  const uint16_t sw[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0xFFE0};
  for (int i = 0; i < 5; i++) {
    s_display.setColor(sw[i]);
    s_display.fillRect(9 + i * 23, 112, 18, 12);
  }
  s_display.endFrame();
}
