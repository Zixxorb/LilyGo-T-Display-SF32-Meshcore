// Bring-up only (step 1 of the port): proves the ST7789 screen works on the SPI bus it shares
// with the radio, before the real display driver exists. Left to right: red, green, blue and
// white bars, and a yellow square in the top left corner (it shows the orientation and the
// colour order at a glance). Replaced by TDeckDisplay.
#include <Arduino.h>
#include <SPI.h>

#define TFT_DC   11
#define TFT_BL   42   // AW9364 backlight driver: held high = full brightness
#define TFT_W    320
#define TFT_H    240

static SPIClass* bus;

static void cmd(uint8_t c, const uint8_t* data = nullptr, size_t n = 0) {
  bus->beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TFT_CS, LOW);
  digitalWrite(TFT_DC, LOW);
  bus->write(c);
  if (n) {
    digitalWrite(TFT_DC, HIGH);
    bus->writeBytes(data, n);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  bus->endTransaction();
}

static uint16_t barColour(int x, int y) {
  if (x < 40 && y < 40) return 0xFFE0;  // yellow corner
  switch (x * 4 / TFT_W) {
    case 0: return 0xF800;  // red
    case 1: return 0x07E0;  // green
    case 2: return 0x001F;  // blue
    default: return 0xFFFF; // white
  }
}

void tdeckTestPattern(SPIClass& spi) {
  bus = &spi;
  pinMode(TFT_DC, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);

  cmd(0x01); delay(150);                          // software reset
  cmd(0x11); delay(120);                          // out of sleep
  const uint8_t colmod = 0x55;  cmd(0x3A, &colmod, 1);  // 16 bits per pixel
  const uint8_t madctl = 0x60;  cmd(0x36, &madctl, 1);  // landscape (MeshCore's rotation 3)
  cmd(0x21);                                      // inversion on (this IPS panel needs it)
  cmd(0x13);                                      // normal mode
  const uint8_t cols[] = {0, 0, (TFT_W - 1) >> 8, (TFT_W - 1) & 0xFF};
  const uint8_t rows[] = {0, 0, (TFT_H - 1) >> 8, (TFT_H - 1) & 0xFF};
  cmd(0x2A, cols, 4);
  cmd(0x2B, rows, 4);

  static uint8_t line[TFT_W * 2];
  bus->beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TFT_CS, LOW);
  digitalWrite(TFT_DC, LOW);
  bus->write(0x2C);                               // memory write
  digitalWrite(TFT_DC, HIGH);
  for (int y = 0; y < TFT_H; y++) {
    for (int x = 0; x < TFT_W; x++) {
      uint16_t c = barColour(x, y);
      line[x * 2] = c >> 8;
      line[x * 2 + 1] = c & 0xFF;
    }
    bus->writeBytes(line, sizeof(line));
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  bus->endTransaction();

  cmd(0x29);                                      // display on
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
}
