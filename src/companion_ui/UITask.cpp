// T-Display-SF32 copy of MeshCore's companion ui-new (examples/companion_radio/ui-new).
//
// This file keeps MeshCore's own structure (splash, message view, begin(), loop(), the
// event handlers); changes for this board are marked "SF32". The board's additions live
// in ui_*.inc next to it, #included below in dependency order (one translation unit, as
// before the split):
//   ui_theme.inc         the "MeshCore" theme: colours, cards, header, labels, page dots
//   ui_widgets.inc       on-screen buttons, daylight-saving rules
//   ui_menu.inc          menu actions, drag scrolling, MenuScreen
//   ui_battery.inc       battery history (Battery page)
//   ui_signal.inc        signal tester (Signal page)
//   ui_meshtime.inc      time from the mesh while the clock is unknown
//   ui_activity.inc      mesh activity: packets per hour, channel use (Activity page)
//   ui_sun.inc           sunrise / sunset (Sun and Info clock faces)
//   ui_adverts.inc       adverts: nearby / mesh, now or on a schedule
//   ui_nodes.inc         My Nodes: starred contacts, last heard, battery on request
//   ui_home.inc          HomeScreen: clock, status, battery, heard, radio, bluetooth,
//                        advert, bot, discover, signal and power pages
//   ui_screens.inc       themes, About, Logs, Time zone, always-on display, Night mode hours
//   ui_radio_editor.inc  radio settings editor
//   ui_qr.inc            share contact (QR code)
//   ui_scan.inc          band scan: noise across 902-928 MHz
//   ui_sos.inc           Mesh SOS
//   ui_prefs.inc         UI settings file (/sf32_ui), time zone offset, screen resume
//   ui_navigation.inc    opening/closing menus, menu actions, touch routing
//   ui_power.inc         battery warnings, burn-in care, motion, night dim, always-on
//                        display, the back gesture
//   ui_bot.inc           Bot: auto-reply to messages sent with 1-byte path hashes
//
// Features: clock page (default), MeshCore theme on full-panel square layout, swipe
// gestures (down: power menu, up: page menu, right: back), Settings (brightness, timeout,
// always-on, night dim, theme, clock & time zone, motion, alerts, sound, Bluetooth, logs,
// power), Bot page (auto-reply), radio editor, About, battery warnings, burn-in care, message chimes.
#include "UITask.h"
#include "SF32Display.h"
#include "SF32TouchInput.h"
#include "SF32Sound.h"
#include "SF32Motion.h"
#include "SF32UsbInterface.h"
#include <helpers/MultiSerialInterface.h>
extern MultiSerialInterface interface_manager;  // companion main.cpp
#include <helpers/rp2040/SerialBLEInterface.h>  // SF32SerialBLEInterface (compat/)

extern SerialBLEInterface bluetooth_interface;  // MeshCore's companion main.cpp
#include <helpers/TxtDataHelpers.h>
#include <MyMesh.h>
#include "target.h"
#include <time.h>
extern volatile bool sf32_ui_animating;  // main.cpp: shorter main-loop sleep while set
extern volatile uint8_t sf32_loop_sleep_ms;  // main.cpp: main-loop sleep
#ifdef ENABLE_WIFI_INTERFACE
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

// Used both for recent adverts and discovered nodes
#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK || UI_HAS_ROTARY_INPUT
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"
#ifdef SF32_LOOP_AB
extern "C" bool sf32_loop_ab_slow(void);
extern "C" { volatile uint32_t sf32_ab_alerts = 0; }
#endif

#include "ui_theme.inc"  // the "MeshCore" theme: colours, cards, header, values, scrolling labels, page dots

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // SF32: MeshCore wordmark, site and version, centred on the panel
    display.setColor(UIColor::primary_txt);
    display.drawXbm(0, 40, meshcore_logo, 128, 13);
    display.setColor(THEME_ACCENT);
    display.fillRect(58, 60, 12, 2);
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    display.drawTextCentered(display.width() / 2, 68, "meshcore.io");
    display.setColor(UIColor::secondary_txt);
    char tmp[40];
    sprintf(tmp, "%s  -  T-Display-SF32", _version_info);
    display.drawTextCentered(display.width() / 2, 82, tmp);
    display.setColor(THEME_DIM);
    display.drawTextCentered(display.width() / 2, 94, FIRMWARE_BUILD_DATE);
    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

#include "ui_widgets.inc"  // on-screen buttons and daylight-saving rules

#include "ui_menu.inc"  // menu actions, drag scrolling, MenuScreen

#include "ui_battery.inc"  // battery history (Battery page graph)
#include "ui_signal.inc"  // signal tester (Signal page)
#include "ui_meshtime.inc"  // time from the mesh (adverts) while the clock is unknown
#include "ui_activity.inc"  // mesh activity (Activity page): packets per hour, channel use
#include "ui_sun.inc"  // sunrise / sunset from the node's location (Sun and Info clock faces)
#include "ui_adverts.inc"  // send adverts now / on a schedule (Settings > Auto advert)
#include "ui_nodes.inc"  // My Nodes (Nodes page): starred contacts, battery on request

#include "ui_home.inc"  // HomeScreen: the pages (clock, status, heard, radio, bluetooth, advert, discover, power)

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[UI_MSG_PREVIEW_SIZE];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }
  int unreadCount() const { return num_unread; }  // SF32
  void clearUnread() { num_unread = 0; }           // SF32
  // SF32: the newest message, for the banner: sender (without the "(hops) " and ":") and text
  void latest(char* from, int from_len, char* text, int text_len) const {
    const MsgEntry* p = &unread[head];
    const char* o = p->origin;
    if (*o == '(') { const char* q = strchr(o, ')'); if (q) o = q + (q[1] == ' ' ? 2 : 1); }
    snprintf(from, from_len, "%s", o);
    int n = strlen(from);
    if (n && from[n - 1] == ':') from[n - 1] = 0;
    snprintf(text, text_len, "%s", p->msg);
  }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    // SF32: themed message view
    char tmp[24];
    themeHeader(display, "Message", _task->getBattMilliVolts());
    auto p = &unread[head];

    themeCard(display, 4, 27, 120, 86);
    char origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(origin, p->origin, sizeof(origin));
    formatAge(tmp, _rtc->getCurrentTime() - p->timestamp);
    display.setTextSize(1);
    int tw = display.getTextWidth(tmp);
    display.setColor(THEME_ACCENT);
    display.drawTextEllipsized(9, 31, 110 - tw - 4, origin);
    display.setColor(UIColor::secondary_txt);
    display.drawTextRightAlign(119, 31, tmp);

    display.setColor(UIColor::primary_txt);
    char msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(msg, p->msg, sizeof(msg));
    display.setCursor(9, 44);
    display.printWordWrap(msg, 110);

    display.setTextSize(1);
    display.setColor(UIColor::secondary_txt);
    if (num_unread > 1) sprintf(tmp, "%d more  -  tap: next  -  hold: done", num_unread - 1);
    else strcpy(tmp, "tap / hold: done");
    display.drawTextCentered(display.width() / 2, 117, tmp);
    return _rtc->getCurrentTime() - p->timestamp < 60 ? 1000 : 30000;  // "Ns" ages tick, "Nm" don't
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      if (num_unread > 0) num_unread--;  // SF32: may already be 0 (the phone downloaded them)
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + autoOffMillis();

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.startup();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  menu = new MenuScreen(this);  // SF32
  createEditors();
  if (_display != NULL) loadUIPrefs();  // SF32: brightness, timeout, clock format
  _auto_off = millis() + autoOffMillis();  // SF32: again, now with the saved timeout (the first one used the 15 s default)
  // SF32: the app protocol over USB as well as Bluetooth (Settings > USB app)
  interface_manager.addInterface(InterfaceType::USB, &usb_app_interface);
  if (_ui_prefs.usb_app) usb_app_interface.enable(); else usb_app_interface.disable();
  motion.setActive(_ui_prefs.lift_wake || _ui_prefs.flip_quiet || _ui_prefs.rotation == 2);  // SF32: accelerometer only if used
  applyRotation();
  motion.begin();                       // SF32: BHI260AP (background)
  InternalFS.remove("/sf32_steps");     // SF32: left over from the removed step counter
  batt_history.begin();                 // SF32
  setCurrScreen(splash);
}

#include "ui_screens.inc"  // Settings screens: themes, About, Logs, Time zone, always-on display, Night mode hours

#include "ui_radio_editor.inc"  // the radio settings editor (overview, value screens, review)

#include "ui_qr.inc"  // share contact as a QR code
#include "ui_scan.inc"  // band scan (Radio page)
#include "ui_sos.inc"  // Mesh SOS: countdown, send to a channel, repeat

#include "ui_prefs.inc"  // time zone offset, screen resume, the UI settings file (/sf32_ui)

#include "ui_navigation.inc"  // opening/closing menus, menu actions, touch routing (taps, swipes, drags)

#include "ui_power.inc"  // battery warnings, burn-in care, motion, night dim, always-on display, back gesture

#include "ui_bot.inc"  // Bot: auto-reply to messages sent with 1-byte path hashes


// SF32: a new message that alerts shows a small banner at the top for a few seconds (sender
// and the start of the text) over whatever is on screen; the full text is in the message
// view, from the envelope on the clock or "Read messages" in the page menu. No typing or
// replying on the device: that's the phone's job.
void UITask::showNewMessage(bool direct) {
  // alert: chime + light the screen briefly (also while a phone is connected)
  bool alert = _ui_prefs.alerts == 0 || (_ui_prefs.alerts == 1 && direct);
  if (_ui_prefs.flip_quiet && motion.faceDown()) alert = false;  // lying face down: quiet
  if (dndNow()) alert = false;  // do not disturb (night hours): no chime, no wake; still unread
#ifdef SF32_LOOP_AB
  if (alert) sf32_ab_alerts++;  // battery test: counted, not shown, so the screen stays dark
  alert = false;
#endif
  if (_aod) {
    if (!alert) { _next_refresh = 0; return; }  // quiet: the envelope on the AOD updates
    exitAOD();
  }
  if (alert) {
    sound.chime(direct ? SF32Sound::CHIME_DIRECT : SF32Sound::CHIME_CHANNEL, (SF32Sound::Volume)_ui_prefs.sound);
    if (_display && !_display->isOn()) {
      _display->turnOn();
      _auto_off = millis() + 6000;  // brief: 6 s, unless touched
    }
  }
  if (alert) {
    ((MsgPreviewScreen*)msg_preview)->latest(_banner_from, sizeof(_banner_from), _banner_text, sizeof(_banner_text));
    if (millis() >= _banner_expiry) { _banner_start = millis(); _banner_first = true; }
    _banner_expiry = millis() + 6000;
  }
  _next_refresh = 0;  // the envelope count (and the banner)
}

// SF32
int UITask::getUnreadCount() const {
  return msg_preview ? ((MsgPreviewScreen *) msg_preview)->unreadCount() : 0;
}

bool UITask::showMessages() {
  if (getUnreadCount() == 0) return false;
  setCurrScreen(msg_preview);
  return true;
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
#ifdef SF32_REMOTE_TOUCH
  duration_millis *= 12;  // debug builds: long enough to screenshot
#endif
  if (millis() >= _alert_expiry) { _alert_start = millis(); _alert_first = true; }  // already up: just the new text
  _alert_expiry = millis() + duration_millis;
}

// How far a popup is in (0..1): eased in after it starts, out before it ends.
static float popupEase(unsigned long start, unsigned long expiry, int in_ms, int out_ms, bool* moving) {
  unsigned long now = millis();
  long el = (long)(now - start), left = (long)(expiry - now);
  float e = 1;
  *moving = false;
  if (el < in_ms) {
    float t = el < 0 ? 0 : (float)el / in_ms;
    e = 1 - (1 - t) * (1 - t) * (1 - t);
    *moving = true;
  }
  if (left < out_ms) {
    float t = left < 0 ? 0 : (float)left / out_ms;
    t = t * t;
    if (t < e) e = t;
    *moving = true;
  }
  return e;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}

void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  // we only want to show text messages on display, not cli data
  if (!(txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN)) return;

  uint8_t path_len = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from.name, text);
  showNewMessage(true);  // SF32: direct
  botCheck(pkt, &from, NULL, text);  // SF32

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + autoOffMillis();  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }

  if (!hasConnection()) {
    notify(UIEventType::contactMessage);
  }
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
  uint8_t path_len = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, channel_details.name, text);
  showNewMessage(false);  // SF32: channel
  botCheck(pkt, NULL, &channel_details, text);  // SF32

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + autoOffMillis();  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }

  if (!hasConnection()) {
    notify(UIEventType::channelMessage);
  }
}

void UITask::onQueueSizeChanged(int msgcount) {
  int before = _msgcount;
  _msgcount = msgcount;
  // SF32: the phone app has downloaded the waiting messages, so they're in its chats and
  // notifications: they stop counting as unread here (the clock's envelope goes). The app
  // never tells the board when a message is actually read; downloading is the only sign.
  // A message open on screen stays until dismissed (stock ui-new jumped to the home
  // screen here, even out of Settings).
  if (msgcount == 0 && before > 0 && hasConnection()) {
    ((MsgPreviewScreen *) msg_preview)->clearUnread();
    _next_refresh = 0;
  }
}

void UITask::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  // SF32: time from the mesh. Only nodes already in the contacts count: brand-new
  // identities cost nothing to make, so a stranger can't outvote them.
  if (_ui_prefs.mesh_time && !is_new && !rtc_clock.timeKnown()) {
    uint32_t t = mesh_time.add(contact.id.pub_key, contact.last_advert_timestamp);
    if (t) {
      rtc_clock.setFromMesh(t);
      if (rtc_clock.timeFromMesh()) {
        board.logEvent("Time set from the mesh");
        _next_refresh = 0;
      }
    }
  }
  if (!hasConnection()) {
    notify(UIEventType::newContactMessage);
  }
}

// SF32: a reply MeshCore's companion code doesn't know: maybe My Nodes' battery request
bool UITask::onUnhandledResponse(const ContactInfo &from, uint32_t tag, const uint8_t* data, uint8_t len) {
  if (!my_nodes.onResponse(from, tag, data, len)) return false;
  _next_refresh = 0;
  return true;
}

void UITask::onControlDataRecv(const mesh::Packet* packet) {
#if UI_DISCOVER_SCREEN
  if (packet->payload_len >= 14 && (packet->payload[0] & 0xF0) == CTL_TYPE_NODE_DISCOVER_RESP) {
    ((HomeScreen *) home)->handleDiscoverResponse(packet);
  }
#endif
  if (packet->payload_len >= 14 && (packet->payload[0] & 0xF0) == CTL_TYPE_NODE_DISCOVER_RESP) {
    signal_tester.onResponse(packet);  // SF32
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){
  batt_history.save();  // SF32

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    display.forceFullRefresh();
    display.clear();
    display.endFrame();
    // Power off board including radio, display, GPS and components
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
  handleTouchMenus();  // SF32
  powerCare();         // SF32
  botLoop();           // SF32
  sosLoop();           // SF32
  if (_qr_bright && (curr != qr_screen || _display == NULL || !_display->isOn())) {
    _qr_bright = false;  // SF32: QR code gone: back to the normal brightness
    _applied_brightness = -1;
    applyBrightness();
  }
  if (batt_history.due()) batt_history.loop(getBattMilliVolts(), board.isExternalPowered());  // SF32
  mesh_activity.loop();  // SF32: packet counts per hour (Activity page)
  advertLoop();          // SF32: scheduled adverts (Settings > Auto advert)
  if (my_nodes.expire()) _next_refresh = 0;  // SF32: My Nodes: no reply
  if (signal_tester.loop(_ui_prefs.sig_interval, (SF32Sound::Volume)_ui_prefs.sound) && curr == home) _next_refresh = 0;
  if (signal_tester.running && _display != NULL && _display->isOn() && !_aod) {
    _auto_off = millis() + autoOffMillis();  // SF32: stays on while testing (stops by itself)
  }
  if (!_startup_sound_done && _display != NULL && _display->isOn()) {
    // SF32: a startup chime, the first thing the speaker plays (switching the audio on pops;
    // this way the pop is the start of the chime). At the Sound volume, or Low if only tap
    // sounds are on; none if both are off.
    _startup_sound_done = true;
    if (_ui_prefs.sound || _ui_prefs.touch_snd) sound.chime(SF32Sound::CHIME_STARTUP, tapVolume());
  }
#ifdef SF32_NOISE_TEST
  // test builds: 40 s phases from 20 s on: screen on/audio off, screen on/audio on, screen
  // off/audio off, screen off/audio on; RSSI sampled 50x/s, a line every 2 s
  static int nt_phase = -1;
  static float nt_sum = 0; static int nt_n = 0, nt_min = 0;
  static unsigned long nt_next_sample = 0, nt_next_print = 0;
  if (millis() > 20000) {
    int ph = (int)((millis() - 20000) / 20000) % 4;
    if (ph != nt_phase) {
      nt_phase = ph;
      if (ph >= 2) { if (_display->isOn()) _display->turnOff(); }
      else if (!_display->isOn()) { _display->turnOn(); _next_refresh = 0; }
      bool audio = ph == 1 || ph == 3;
      sound.keepOpen(audio, SF32Sound::VOL_LOW);
      _audio_open = audio;  // (the normal logic below leaves it alone: same state)
      _audio_open_vol = SF32Sound::VOL_LOW;
      rt_kprintf("noise: phase %d (screen %s, audio %s)\n", ph, ph < 2 ? "on" : "off", audio ? "on" : "off");
    }
    if (ph < 2) _auto_off = millis() + 60000;
    if (millis() > nt_next_sample) {
      nt_next_sample = millis() + 20;
      int r = (int)radio_driver.getCurrentRSSI();
      if (r < 0) { nt_sum += r; if (nt_n == 0 || r < nt_min) nt_min = r; nt_n++; }
    }
    if (millis() > nt_next_print) {
      nt_next_print = millis() + 2000;
      if (nt_n) rt_kprintf("noise: phase %d rssi avg %d min %d (n %d) floor %d rx %d busy %d sent %lu recv %lu err %lu\n",
                           nt_phase, (int)(nt_sum / nt_n), nt_min, nt_n, (int)radio_driver.getNoiseFloor(),
                           (int)radio_driver.isInRecvMode(), (int)radio_driver.isReceiving(),
                           (unsigned long)radio_driver.getPacketsSent(), (unsigned long)radio_driver.getPacketsRecv(),
                           (unsigned long)radio_driver.getPacketsRecvErrors());
      rt_kprintf("noise: charging %d usb %d batt %d mV ble %d connected %d\n", (int)board.isChargerActive(),
                 (int)board.isExternalPowered(), (int)board.getBattMilliVolts(), (int)isBluetoothEnabled(), (int)hasConnection());
      nt_sum = 0; nt_n = 0;
    }
  }
#else
  {  // SF32: the audio path stays open (silent) while any sound is on and the screen is in
     // use, and for 2 min after it went off (or to the always-on clock): powering the
     // amplifier and DAC up pops, so quick re-wakes don't. After that it powers down (a few
     // mA saved); the next wake or chime powers it up again (one pop). Both sounds Off: down.
    // (and for a minute after either was changed: cycling Tap sound passes Off, which
    // would power down and pop again on the next style)
    if (_audio_grace_until && (long)(millis() - _audio_grace_until) >= 0) _audio_grace_until = 0;  // (0: none)
    bool in_use = _display != NULL && _display->isOn() && !_aod;
    if (in_use) _screen_off_since = 0;
    else if (!_screen_off_since) _screen_off_since = millis() | 1;  // (0: in use)
    bool recent = in_use || millis() - _screen_off_since < 2UL * 60 * 1000;
    bool want = ((_ui_prefs.sound || _ui_prefs.touch_snd) && recent) || _audio_grace_until;
    SF32Sound::Volume v = tapVolume();
    if (want != _audio_open || (want && v != _audio_open_vol)) {
      _audio_open = want;
      _audio_open_vol = v;
      sound.keepOpen(want, v);
    }
  }
#endif
#ifdef SF32_SOUND_STRESS
  {  // test builds: a tap sound every second and a Sound volume change every 4 s
    static unsigned long next = 15000;
    static int k = 0;
    if (millis() > next) {
      next = millis() + 1000;
      if (++k % 4 == 0) menuAction(ACT_SET_SOUND);
      else if (_ui_prefs.touch_snd) sound.tap(_ui_prefs.touch_snd, tapVolume());
    }
  }
#endif
  if (_tap_preview_at && (sound.ready() || (long)(millis() - _tap_preview_at) >= 0)) {  // SF32: Tap sound preview
    _tap_preview_at = 0;
    if (_ui_prefs.touch_snd) sound.tap(_ui_prefs.touch_snd, tapVolume());
  }
  {  // SF32: power: the screen off (and no audio) for 5 s: CPU to 96 MHz (lower core
     // voltage) and a 25 ms main loop; full speed again as the screen turns on (the
     // display driver asks for it) or for audio (the sound thread does)
    bool screen_on = _display != NULL && _display->isOn();
    bool idle = !screen_on && !sound.running();
    if (!idle) _cpu_idle_since = 0;
    else if (!_cpu_idle_since) _cpu_idle_since = millis() | 1;
#ifdef SF32_CPU_TOGGLE_TEST
    // test builds: screen off from 20 s, the CPU alternating 96 / 240 MHz every 15 s
    if (millis() > 20000) {
      if (screen_on) _display->turnOff();
      board.setCpuLow(((millis() - 20000) / 15000) % 2 == 0);
    }
    (void)idle;
#else
    if (idle && millis() - _cpu_idle_since > 5000) board.setCpuLow(true);
    else if (!idle) board.setCpuLow(false);
#endif
#ifdef SF32_LOOP_AB
    sf32_loop_sleep_ms = screen_on && !_aod ? 4 : (sf32_loop_ab_slow() ? 250 : 25);
#else
    sf32_loop_sleep_ms = screen_on && !_aod ? 4 : 25;
#endif
    // sleep (power manager): awake while the screen (always-on too) or audio is on, the
    // first minute after boot, or with Power saving off
    board.holdAwake(SF32Board::AWAKE_SCREEN, screen_on);
    board.holdAwake(SF32Board::AWAKE_AUDIO, sound.running());
    board.holdAwake(SF32Board::AWAKE_USER, !_ui_prefs.power_save);
    // and on USB power: the debug channel (flashing, screenshots) can't reach a sleeping chip,
    // and on the charger sleep saves nothing that matters. (Test builds: sleep on USB for the
    // first 8 minutes to measure it, then stay awake so the tools can reach it again.)
#ifdef SF32_PM_LOG
    board.holdAwake(SF32Board::AWAKE_USB, board.isExternalPowered() && millis() > 8UL * 60 * 1000);
#else
    board.holdAwake(SF32Board::AWAKE_USB, board.isExternalPowered());
#endif
    if (millis() > 60000) board.holdAwake(SF32Board::AWAKE_BOOT, false);
#ifdef SF32_PM_LOG
    {
      static unsigned long next_pm = 0, pm_loops = 0;
      pm_loops++;
      if (millis() > next_pm) {
        next_pm = millis() + 5000;
        uint32_t sleeps, slept = board.takeSleepStats(&sleeps);
        rt_kprintf("pm: slept %lu ms in %lu sleeps / 5 s, loops %lu, millis %lu rtc %lu, recv %lu, rssi %d, screen %d audio %d\n",
                   (unsigned long)slept, (unsigned long)sleeps, pm_loops, (unsigned long)millis(),
                   (unsigned long)rtc_clock.getCurrentTime(), (unsigned long)radio_driver.getPacketsRecv(),
                   (int)radio_driver.getCurrentRSSI(), (int)screen_on, (int)sound.running());
        rt_kprintf("pm: ble on %d, phone connected %d\n", (int)isBluetoothEnabled(), (int)hasConnection());
        pm_loops = 0;
      }
      // at 210 s (asleep by then, screen off): the alert a phone DM triggers, plus a chime:
      // waking from sleep, the display and opening audio, on the real path
      static bool alert_done = false;
      if (!alert_done && millis() > 210000 && !screen_on) {
        alert_done = true;
        uint32_t sl;
        rt_kprintf("pm: alert test (slept %lu ms in the last stretch)\n", (unsigned long)board.takeSleepStats(&sl));
        showNewMessage(true);
        sound.chime(SF32Sound::CHIME_TEST, SF32Sound::VOL_LOW);
      }
      // a zero-hop discovery ping every 30 s while the screen is off: replies prove the
      // radio sends and hears while the chip sleeps (recv climbs)
      static unsigned long next_ping = 0;
      if (!screen_on && millis() > next_ping) {
        next_ping = millis() + 30000;
        uint8_t cmd[6] = {CTL_TYPE_NODE_DISCOVER_REQ | 1, 0xFF};
        the_mesh.getRNG()->random(&cmd[2], 4);
        mesh::Packet* req = the_mesh.createControlData(cmd, sizeof(cmd));
        if (req) { the_mesh.sendZeroHop(req); rt_kprintf("pm: ping sent\n"); }
      }
    }
#endif
    // (no touch controller low-power mode: in it the CST9220 stops reporting taps, so a tap
    // can't wake the screen; tried 2026-11-01)
#ifdef SF32_CPU_TEST
    static unsigned long next_log = 0, loops = 0;
    loops++;
    if (millis() > next_log) {
      next_log = millis() + 2500;
      rt_kprintf("cpu: low %d hclk %lu MHz millis %lu rtc %lu screen %d rssi %d usb %d loops/5s %lu\n", (int)board.cpuLow(),
                 (unsigned long)board.cpuMHz(), (unsigned long)millis(),
                 (unsigned long)rtc_clock.getCurrentTime(), (int)screen_on, (int)radio_driver.getCurrentRSSI(),
                 (int)board.isExternalPowered(), loops);
      loops = 0;
    }
#endif
  }
  if (uint8_t touch = touch_input.takeClick()) {  // SF32: touch feedback
#ifdef SF32_SOUND_LOG
    rt_kprintf("tap: %s, touch_snd %d aod %d\n", touch == 2 ? "swipe" : "tap", _ui_prefs.touch_snd, _aod);
#endif
    // taps: the chosen style; swipes: a swish. At the Sound volume (Low if chimes are off).
    if (_ui_prefs.touch_snd && !_aod && !_tap_preview_at) {  // (a preview is coming: it's the sound)
      if (touch == 2) sound.chime(SF32Sound::CHIME_SWISH, tapVolume());
      else sound.tap(_ui_prefs.touch_snd, tapVolume());
    }
  }
  if (!_bt_restored) {  // SF32: Bluetooth on/off as last set (after MeshCore enabled it)
    _bt_restored = true;
    if (_ui_prefs.bt_off) disableBluetooth();
  } else if (isBluetoothEnabled() == (bool)_ui_prefs.bt_off) {
    _ui_prefs.bt_off = !isBluetoothEnabled();  // toggled from a page, menu or the app
    saveUIPrefs();
  }
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  #ifdef UI_HAS_NAV_INPUT
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    display.turnOff();
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_SELECT);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
  #else
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    openSos();  // SF32: triple press of button A = Mesh SOS (5 s countdown first)
    c = 0;
  }
  #endif  
#endif
#if defined(UI_HAS_ROTARY_INPUT)
  RotaryInputEvent rotaryEv = rotary_input.poll();
  if (rotaryEv != RotaryInputEvent::None && _display != NULL && _display->isOn()) {
    _auto_off = millis() + autoOffMillis();  // SF32: any swipe counts as activity
  }
  bool sub_screen = curr == menu || curr == theme_screen || curr == about_screen || curr == logs_screen ||
                    curr == tz_screen || curr == hour_screen || curr == sos_screen || curr == qr_screen || curr == scan_screen ||
                    curr == msg_preview || isRadioScreen();
  if (rotaryEv == RotaryInputEvent::Prev && sub_screen && _display != NULL && _display->isOn()) {
    goBack();  // SF32: swipe right (finger left -> right) = back
    rotaryEv = RotaryInputEvent::None;
  }
  if (rotaryEv == RotaryInputEvent::Next && curr == msg_preview && _display != NULL && _display->isOn()) {
    c = KEY_NEXT;  // SF32: in the messages, swipe left = the next one
  }
  if (c == 0 && _display != NULL && _display->isOn() && !sub_screen) {  // SF32: pages only
    if (rotaryEv == RotaryInputEvent::Next) {
      c = KEY_NEXT;
    } else if (rotaryEv == RotaryInputEvent::Prev) {
      c = KEY_PREV;
    }
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    int page_before = curr == home ? ((HomeScreen *) home)->currentPage() : -1;
    curr->handleInput(c);
    if (curr == home && page_before >= 0 && ((HomeScreen *) home)->currentPage() != page_before) {  // SF32: slide
      bool back = (c == KEY_PREV || c == KEY_LEFT);
      ((SF32Display *) _display)->setTransition(back ? SF32Display::TRANSITION_RIGHT : SF32Display::TRANSITION_LEFT);
    }
    _auto_off = millis() + autoOffMillis();   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();
  sf32_ui_animating = _display != NULL && _display->isOn() && (_drag_on || (long)(_next_refresh - millis()) <= 20);  // SF32

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr && !_drag_on) {
      unsigned long frame_start = millis();  // SF32: pace animation from the frame's start
#ifdef SF32_FRAME_LOG
      rt_tick_t f0 = rt_tick_get();
#endif
      ((SF32Display *) _display)->setGlassPaused(_aod);  // the always-on clock stays on black
      if (curr != menu) ((SF32Display *) _display)->cancelListFrame();  // left the list meanwhile
      _display->startFrame();
      ((SF32Display *) _display)->setSquareLayout(true);  // SF32: all screens are full-panel
      int delay_millis = curr->render(*_display);
#ifdef SF32_FRAME_LOG
      rt_tick_t f1 = rt_tick_get();
#endif
      if (_hold_frac > 0 && curr == home) {  // SF32: the hold-to-change-face ring
        SF32Display* sd = (SF32Display*)_display;
        int lx, ly;
        SF32Display::panelToLogical(touch_input.fingerX0(), touch_input.fingerY0(), lx, ly);
        const float R = 13, PI2 = 6.2831853f;
        int n = (int)(_hold_frac * 24);
        for (int k = 0; k <= 24; k++) {
          float a = k * PI2 / 24;
          float x = lx + sinf(a) * R, y = ly - cosf(a) * R;
          sd->setColor(k <= n ? THEME_ACCENT : THEME_DIM);
          sd->drawLineAA(x, y, x, y, k <= n ? 2.6f : 1.4f);
        }
      }
      bool popup_moving = false, popup_full = false;  // an eased popup: its frames come quickly
      if (millis() < _banner_expiry) {  // SF32: new message banner (top): slides down, and back up
        bool moving;
        float e = popupEase(_banner_start, _banner_expiry, 240, 180, &moving);
        popup_moving |= moving;
        if (_banner_first) { popup_full = true; _banner_first = false; }
        int by = 3 - (int)((1 - e) * 38 + 0.5f);
        char from[sizeof(_banner_from)], text[sizeof(_banner_text)];
        _display->translateUTF8ToBlocks(from, _banner_from, sizeof(from));
        _display->translateUTF8ToBlocks(text, _banner_text, sizeof(text));
        ((SF32Display*)_display)->setPopupFade(e);
        _display->setColor(UIColor::popup_bkg);
        _display->fillRect(4, by, 120, 34);
        _display->setColor(THEME_ACCENT);
        _display->fillRect(4, by + 4, 2, 26);
        _display->setTextSize(1);
        _display->drawTextEllipsized(10, by + 3, 110, from);
        _display->setColor(UIColor::popup_txt);
        // two lines at most (the rest is in the message view): the first broken at a word
        const char* t = text;
        int n = 0, cut = 0;
        char line[48];
        while (t[n] && n < 47) {
          memcpy(line, t, n + 1); line[n + 1] = 0;
          if (_display->getTextWidth(line) > 110) break;
          n++;
          if (t[n] == ' ' || t[n] == 0) cut = n;
        }
        if (!t[n]) cut = n;   // all of it fits
        if (cut == 0) cut = n;  // one long word
        memcpy(line, t, cut); line[cut] = 0;
        _display->setCursor(10, by + 13);
        _display->print(line);
        t += cut;
        while (*t == ' ') t++;
        if (*t) _display->drawTextEllipsized(10, by + 23, 110, t);
      }
      bool alert_up = millis() < _alert_expiry;
      if (alert_up) {  // render alert popup (SF32: themed card): grows in, and shrinks away
        bool moving;
        float e = popupEase(_alert_start, _alert_expiry, 200, 150, &moving);
        popup_moving |= moving;
        if (_alert_first) { popup_full = true; _alert_first = false; }
        float scale = 0.84f + 0.16f * e;
        int aw = (int)(108 * scale + 0.5f), ah = (int)(30 * scale + 0.5f);
        ((SF32Display*)_display)->setPopupFade(e);
        _display->setColor(UIColor::popup_bkg);
        _display->fillRect((128 - aw) / 2, 63 - ah / 2, aw, ah);
        if (e > 0.6f) {
          _display->setColor(THEME_ACCENT);
          _display->fillRect(58, 63 + ah / 2 - 6, 12, 2);
          _display->setTextSize(1);
          ColorVal txt = UIColor::popup_txt;  // fades in with it
          int f = (int)((e - 0.6f) / 0.4f * 31 + 0.5f);
          _display->setColor((ColorVal)((((txt >> 11) * f / 31) << 11) | ((((txt >> 5) & 0x3F) * f / 31) << 5) | ((txt & 0x1F) * f / 31)));
          _display->drawTextCentered(_display->width() / 2, 59, _alert);
        }
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        // SF32: animating (scrolling, gliding): the next frame starts as soon as this one's
        // time is up, counted from when it started, not after drawing it as well
        _next_refresh = (delay_millis <= 20 ? frame_start : millis()) + delay_millis;
      }
      ((SF32Display*)_display)->setPopupFade(1);
      if (popup_moving) {
        _next_refresh = millis() + 16;
        // only the popup moves: send just its band, once the frame it first appears on has gone
        // whole (the page may have just changed). The frame after the easing is a whole one too.
        bool banner_up = millis() < _banner_expiry;
        if (!popup_full && !(banner_up && alert_up)) {
          if (banner_up) ((SF32Display*)_display)->setFlushBand(0, 42);
          else ((SF32Display*)_display)->setFlushBand(40, 48);
        }
      }
      // SF32: the banner's end needs a frame too
      if (millis() < _banner_expiry && (long)(_next_refresh - _banner_expiry) > 0) _next_refresh = _banner_expiry;
      _display->endFrame();
#ifdef SF32_FRAME_LOG
      static rt_tick_t last_frame = 0;
      rt_kprintf("frame: draw %d ms, flush %d ms, since last %d ms\n", (int)(f1 - f0), (int)(rt_tick_get() - f1),
                 (int)(f0 - last_frame));
      last_frame = f0;
#endif
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      _auto_off = millis() + autoOffMillis();
    }
#endif
    if (millis() > _auto_off && !_aod) {
      if (_ui_prefs.aod && !(_ui_prefs.flip_quiet && motion.faceDown()) && curr != splash) {
        enterAOD();  // SF32: always-on clock instead of a dark screen
      } else {
        _display->turnOff();
        _display_off_at = millis();  // SF32: resumeOrReset() on wake
      }
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
      if(!board.isExternalPowered()) {
        if (_display != NULL) {
          _display->startFrame();
          _display->setTextSize(2);
          _display->setColor(UIColor::warning_txt);
          _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
          _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
          _display->endFrame();
          if (_display->isEink() == false) { delay(3000); }
        }
        shutdown();
      }
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      resumeOrReset();      // SF32: back where it was if off < 30 s, else the clock
      _display->turnOn();   // turn display on and consume event
      c = 0;
    } else if (_aod) {      // SF32: always-on display: back to the full UI, consume
      exitAOD();
      c = 0;
    } else if (_dimmed) {   // SF32: the first touch on a dimmed screen only brightens it
      c = 0;
    }
    _auto_off = millis() + autoOffMillis();   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double-click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
