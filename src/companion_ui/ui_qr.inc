// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Contact QR: this node as a QR code the MeshCore app scans to add it
// (meshcore://contact/add?name=...&public_key=...&type=1, see MeshCore docs/qr_codes.md).
// From the swipe-up menu on the Clock and Status pages. Black modules on a white card at
// full brightness while shown (easier to scan); tap or swipe to close.
// The same screen is the flashlight (power menu > Flashlight): the whole panel lit at full
// brightness, white or red (keeps night vision; tap switches); swipe or button A closes.

#include <ctype.h>
#include "../qrcodegen/qrcodegen.h"

#define QR_MAX_VERSION 10  // 57x57 modules: far more than a contact needs

static void urlEncode(char* out, size_t out_len, const char* in) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for (; *in && o + 4 < out_len; in++) {
    unsigned char c = (unsigned char)*in;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = c;
    else if (c == ' ') out[o++] = '+';
    else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
  }
  out[o] = 0;
}

class QrScreen : public UIScreen {
  UITask* _task;
  NodePrefs* _prefs;
  uint8_t _qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
  bool _ok = false;
  bool _flash = false, _red = false;

public:
  QrScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}
  void setFlashlight(bool on) { _flash = on; _red = false; }
  bool flashlight() const { return _flash; }
  void toggleRed() { _red = !_red; }

  void build() {
    char name[3 * sizeof(_prefs->node_name) + 1], key[2 * PUB_KEY_SIZE + 1], url[256];
    urlEncode(name, sizeof(name), _prefs->node_name);
    mesh::Utils::toHex(key, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    for (char* p = key; *p; p++) *p = tolower(*p);  // as in MeshCore's docs
    snprintf(url, sizeof(url), "meshcore://contact/add?name=%s&public_key=%s&type=1", name, key);
    static uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
    _ok = qrcodegen_encodeText(url, temp, _qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                               QR_MAX_VERSION, qrcodegen_Mask_AUTO, true);
  }

  int render(DisplayDriver& display) override {
    SF32Display& d = (SF32Display&)display;
    if (_flash) {
      d.fillWholePanel(_red ? 0xF800 : 0xFFFF);
      display.setTextSize(1);
      display.setColor(_red ? 0x7800 : 0x8410);
      display.drawTextCentered(UI_CX, 118, _red ? "tap: white" : "tap: red");
      return 60000;
    }
    display.setTextSize(1);
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(UI_CX, 3, "Scan with the MeshCore app");
    if (!_ok) {
      display.setColor(UIColor::warning_txt);
      display.drawTextCentered(UI_CX, 60, "Couldn't make the code");
      return 5000;
    }
    // QR with a 4-module quiet zone, whole panel pixels per module, centred
    int size = qrcodegen_getSize(_qr);
#ifdef VESPER_TDECK  // 320x240, the UI's square at 1.875x from the top: the same place in UI px
    int m = 185 / (size + 8);
    int box = (size + 8) * m;
    int x0 = 160 - box / 2, y0 = 22;
#else
    int m = 345 / (size + 8);
    int box = (size + 8) * m;
    int x0 = 240 - box / 2, y0 = 58;
#endif
    display.setColor(0xFFFF);
    d.fillPanelRect(x0, y0, box, box);
    display.setColor(0x0000);
    for (int y = 0; y < size; y++) {
      for (int x = 0; x < size;) {
        if (!qrcodegen_getModule(_qr, x, y)) { x++; continue; }
        int run = 1;  // a run of dark modules in one rectangle
        while (x + run < size && qrcodegen_getModule(_qr, x + run, y)) run++;
        d.fillPanelRect(x0 + (4 + x) * m, y0 + (4 + y) * m, run * m, m);
        x += run;
      }
    }
    // the node's name under it
    char filtered[sizeof(_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered, _prefs->node_name, sizeof(filtered));
    display.setTextSize(2);
    display.setColor(UIColor::primary_txt);
    if (display.getTextWidth(filtered) > UI_W - 8) display.drawTextEllipsized(4, 110, UI_W - 8, filtered);
    else display.drawTextCentered(UI_CX, 110, filtered);
    return 60000;
  }

  bool handleInput(char c) override {  // button A: close
    if (c == 0) return false;
    _task->closeQr();
    return true;
  }
};

void UITask::openQr() {
  if (!qr_screen) qr_screen = new QrScreen(this, _node_prefs);
  ((QrScreen*)qr_screen)->setFlashlight(false);
  ((QrScreen*)qr_screen)->build();
  _qr_return = _menu_return ? _menu_return : home;
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_LEFT);
  ((SF32Display*)_display)->setBrightness(100);  // easier to scan (loop() restores it)
  _qr_bright = true;
  _auto_off = millis() + 60000;                    // a minute to get the phone out
  setCurrScreen(qr_screen);
}

void UITask::openFlashlight() {
  if (_aod) exitAOD();
  if (!qr_screen) qr_screen = new QrScreen(this, _node_prefs);
  ((QrScreen*)qr_screen)->setFlashlight(true);
  _qr_return = _menu_return ? _menu_return : home;
  _menu_return = NULL;
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_UP);
  ((SF32Display*)_display)->setBrightness(100);  // loop() restores it when it closes
  _qr_bright = true;
  _auto_off = millis() + 5UL * 60 * 1000;  // stays lit 5 min (a tap restarts the timeout)
  setCurrScreen(qr_screen);
}

bool UITask::flashlightOn() const { return curr == qr_screen && ((QrScreen*)qr_screen)->flashlight(); }

void UITask::closeQr() {
  ((SF32Display*)_display)->setTransition(SF32Display::TRANSITION_RIGHT);
  setCurrScreen(_qr_return ? _qr_return : home);
  _qr_return = NULL;
}
