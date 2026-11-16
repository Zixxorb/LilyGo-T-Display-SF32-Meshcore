// MeshCore DisplayDriver for the T-Display-SF32's 480x480 CO5300 AMOLED.
//
// MeshCore's UIs are laid out for a 128x64 OLED with the classic 6x8 GFX font. We keep
// those logical coordinates but draw straight into the panel's frame buffer at
// SCALE physical pixels per logical pixel (448x224, centred), with anti-aliased
// proportional text (SF32Font.h, from tools/gen_font.py), smoothed icons, round dots
// and rounded panels, instead of blowing up a 128x64 bitmap. getTextWidth() reports
// the real (proportional) width in logical pixels, so MeshCore's centring, right
// alignment and ellipsizing still line up.
#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <rtthread.h>
#include <rtdevice.h>

class SF32Display : public DisplayDriver {
public:
  static const int W = 128, H = 64;
  static constexpr float SCALE = 3.5f;  // must match tools/gen_font.py

  SF32Display() : DisplayDriver(W, H) {}
  bool begin();

  // Board-specific (our companion UI's clock page): lay out this frame as 128x128
  // logical pixels over the whole panel instead of 128x64. startFrame() resets it.
  void setSquareLayout(bool square);
  // Animate the next endFrame(): the new frame slides in, pushing the shown one out.
  enum Transition { TRANSITION_NONE, TRANSITION_LEFT, TRANSITION_RIGHT, TRANSITION_UP, TRANSITION_DOWN };
  void setTransition(Transition t) { if (_on) _transition = t; }

  // Scrolling lists: draw the whole list once into an off-screen layer (beginLayer(height)
  // ... draw at logical y from 0 ... endLayer()), then each frame copy the visible slice
  // with composeLayer(screen rect, first layer row in panel px). Fast enough to follow a
  // finger. beginLayer returns the layer height in panel px.
  int beginLayer(int h, bool clear = true);  // clear = false: the caller clears what it draws
  void clearRows(int y, int h);  // whole-width rows to the background (logical y, h)
  void resumeLayer();  // draw into the layer again, as it is (to update part of it)
  uint32_t layerGen() const;  // changes whenever beginLayer() starts a new layer: still ours?
  void endLayer();
  void composeLayer(int x, int y, int w, int h, int src_px);
  // After drawing a layer that's mostly background (About, Logs, Friends): note where each row's
  // content is, so composing skips the rest without reading it (the layer is in slow PSRAM).
  // Any later drawing into the layer forgets it.
  void layerSpans();
  // The same for a layer of things drawn on the plain background (no panes), such as Starlits:
  // only what differs from the background is copied (in Glass, the wallpaper shows behind).
  void composeSprites(int x, int y, int w, int h, int src_px);
  // low power mode: slides at most 30 fps (the UI caps its own frames the same)
  static bool s_cap30;
  static void setCap30(bool on) { s_cap30 = on; }
  // The same for a menu's rows (x = 4..124), by the graphics hardware: faster. layerRowsDone after
  // drawing rows into the layer (logical). composeList returns false if it couldn't: use composeLayer.
  void layerRowsDone(int y, int h);
  bool composeList(int y, int h, int src_px);
  static int listRows();  // how many layer rows (panel px) composeList can take

  // Only rows y..y+h (logical) changed in this frame (a scrolling list): send just that
  // band to the panel (faster). For the next endFrame() only; ignored with a transition.
  void setFlushBand(int y, int h);
  // A page following the finger. Draw the page coming in as a normal frame (startFrame, render),
  // then dragFrameDone() instead of endFrame(). dragShow: shown `off` panel pixels in, next =
  // coming from the right. sendBusy: the panel is still taking the last one. dragSettle: let go.
  void dragFrameDone();
  bool sendBusy();
  void dragShow(int off, bool next);
  void dragSettle(int off, bool next, bool commit);
  // A scrolling list: the next frame only changes the list, so don't redraw the rest of it (the
  // list is built in composeLayer and only its band is sent). For the next startFrame() only.
  void nextFrameIsList() { _list_next = true; }
  void cancelListFrame() { _list_next = false; }
  bool listFrame() const { return _list_frame; }

  void setBrightness(uint8_t percent);
  void waitFlush();  // until the frame being sent to the panel (in the background) is done
  uint8_t brightness() const { return _brightness; }
  // Burn-in care: move the whole layout by (dx, dy) panel pixels (a few at most).
  void setPixelShift(int dx, int dy);  // applied now if on, and at every turnOn()
  // Panel pixel -> logical coordinates in the square layout (touch hit-testing).
  static void panelToLogical(int px, int py, int& x, int& y);
  // Clip all drawing to a logical rectangle (scrolling text inside a card); clearClip() ends it.
  void setClip(int x, int y, int w, int h);
  void clearClip() { _clip_on = false; }
  // Nested clip (one level): the intersection with the current clip; popClip() restores it.
  void pushClip(int x, int y, int w, int h);
  void popClip();
  // setCursor with a fractional x (smooth scrolling text)
  void setCursorFine(float x, int y);
  // An anti-aliased line with round ends, logical coordinates (fractions allowed), width in
  // logical px (clock hands, dial ticks)
  void drawLineAA(float x0, float y0, float x1, float y1, float width);
  // a 3D-looking shaded ellipse (lit from the upper left, glossy highlight), rotated by rot radians
  void fillShaded(float cx, float cy, float rx, float ry, uint16_t base, float gloss = 0.5f, float rot = 0);
  // a soft spot fading out from the centre (shadows, blush, glow); alpha 0..15 at the middle
  void fillSoft(float cx, float cy, float rx, float ry, uint16_t c, int alpha);
  // A sharp rectangle in panel pixels (0..479, plus the burn-in shift), e.g. QR modules
  void setRotation(int rot);  // quarter turns clockwise, 0..3 (the panel does it: free)
  int rotation() const { return _rot; }
  void fillWholePanel(uint16_t color);  // every pixel, outside the drawing area too
  void fillPanelRect(int px, int py, int w, int h);
  // drawXbm, drawn `scale` times larger (smoothed), e.g. 1.5 for big page icons
  void drawXbmScaled(int x, int y, const uint8_t* bits, int w, int h, float scale);

  // The Glass style: a wallpaper behind everything (background: 0 aurora, 1 dunes, 2 gradient, 3 none),
  // in colours taken from `accent` (multi: several hues), and cards (fillRect in the card or popup colour) become frosted panes: a
  // blurred copy of the wallpaper with a bright rim. No other drawing changes.
  void setGlass(bool on, uint16_t accent, bool multi, uint8_t background);
  bool glassOn() const;
  void setGlassPaused(bool paused);  // plain black meanwhile (the always-on clock)
  // a glint of light on the glass at panel point (x, y) (it follows the board's tilt: UITask)
  void setGlassLight(bool on, int x, int y);
  void setPopupFade(float fade);     // 0..1: how far a Glass popup has faded in
  void glassBadge(int x, int y, int d);  // a round pane behind a big icon
  bool transitionPending() const { return _transition != TRANSITION_NONE; }

  bool isOn() override { return _on; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  static constexpr int TEXT_BATTERY = 9;  // text size: the clock page's battery % (1.5x; digits, %, - only)
  void setTextSize(int sz) override { _text_size = sz < 1 ? 1 : sz; }
  void setColor(ColorVal c) override { _color = c; }
  void setCursor(int x, int y) override { _cx = x; _cy = y; _pen_valid = false; }
  void print(const char* str) override;
  void printWordWrap(const char* str, int max_width) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;

private:
  int drawChar(int pen16, int y, unsigned char c);  // returns the advance (1/16 px)
  int charAdvance(unsigned char c) const;            // 1/16 px
  void blendPixel(int px, int py, int alpha);  // alpha 0..15, _color over the frame
  // the drawable area (panel, bleed and any clip), as inclusive pixel bounds; false: nothing
  bool clipBounds(int& x0, int& y0, int& x1, int& y1) const;
  void fillPhys(int x0, int y0, int x1, int y1, int radius);
  void fillSpan(int x0, int x1, int py);
  void flush(const uint16_t* buf, int py0 = 0, int py1 = -1);  // rows [py0, py1]
  void sendPart(const uint16_t* buf, int sx, int sy, int dx, int dy, int w, int h, bool sync);  // a slide: buf's rect, straight to the panel
  void animate(Transition dir);
  void slideTo(Transition dir, int off);
  void glassFill(int x0, int y0, int x1, int y1, int radius);
  void glassPopup(int x0, int y0, int x1, int y1, int radius);
  void glassRim(int x0, int y0, int x1, int y1, int radius, bool popup);

  rt_device_t _lcd = nullptr;
  bool _on = false;
  int _rot = 0;
  uint8_t _brightness = 80;
  Transition _transition = TRANSITION_NONE;
  bool _wake_pending = false;
  bool _list_next = false, _list_frame = false;
  int _band_y0 = -1, _band_y1 = -1;  // setFlushBand, panel rows
  bool _clip_on = false;
  int _clip_x0 = 0, _clip_y0 = 0, _clip_x1 = 0, _clip_y1 = 0;  // physical
  bool _saved_on = false;
  int _saved_x0 = 0, _saved_y0 = 0, _saved_x1 = 0, _saved_y1 = 0;  // powered on, dark until the first new frame
  int _cx = 0, _cy = 0, _text_size = 1;
  int _pen16 = 0;           // physical x (1/16 px) where the last print() stopped
  bool _pen_valid = false;  // consecutive prints continue from _pen16
  ColorVal _color = 0xFFFF;
};
