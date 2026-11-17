#pragma once

#include "SF32Sound.h"  // SF32: tap sound volume
#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include <AbstractUITask.h>
#include <NodePrefs.h>

#ifdef UI_NO_DISCOVER_SCREEN
  #error "UI_NO_DISCOVER_SCREEN is obsolete - use -D UI_DISCOVER_SCREEN=0 instead"
#endif

#ifndef UI_DISCOVER_SCREEN
  #define UI_DISCOVER_SCREEN  1
#endif

#if UI_DISCOVER_SCREEN
  #ifdef UI_RECENT_LIST_SIZE
    #define DISCOVERED_NODES_TABLE_SIZE UI_RECENT_LIST_SIZE
  #else
    #define DISCOVERED_NODES_TABLE_SIZE 4
  #endif

struct DiscoveredNode {
  float snr_in;
  float snr_out;
  uint8_t pubkey_prefix[9];
  uint8_t type;
  char name[32];
};
#endif

// SF32: UI settings, saved in /sf32_ui
struct SF32UIPrefs {
  uint32_t magic = 0;
  uint8_t brightness_idx = 2;  // 80%
  uint8_t timeout_idx = 0;     // 15 s
  uint8_t clock24 = 0;
  uint8_t dst_rule = 0;        // DstRule: off / US / EU
  uint8_t theme = 0;           // THEMES[] index (added later: older files are 8 bytes)
  uint8_t alerts = 1;          // message alerts: 0 all, 1 direct only, 2 off
  uint8_t sound = 1;           // chime volume: 0 off, 1 low, 2 high, 3 medium
  uint8_t bt_off = 0;          // Bluetooth turned off by the user
  uint8_t lift_wake = 1;       // motion: pick up to wake the screen
  uint8_t flip_quiet = 1;      // motion: face down = no alerts, screen off
  uint8_t steps_on = 1;        // unused (was the pedometer; kept for the file layout)
  uint8_t night_on = 0;        // night dim
  uint8_t night_from = 22;     // hour (local)
  uint8_t night_until = 7;
  uint8_t night_level = 1;     // NIGHT_LEVELS index (10 %)
  uint8_t aod = 0;             // always-on display
  uint8_t bot_on = 0;          // bot: auto-reply to 1-byte path hash messages
  uint8_t bot_dm = 1;          //   reply to direct messages
  uint8_t bot_chan = 1;        //   channel messages: 0 ignore, 1 DM the sender (if a contact), 2 reply in the channel (@[name])
  uint8_t touch_snd = 1;       // a sound on taps and swipes: 0 off, 1 click, 2 pop, 3 tick
  uint8_t sig_interval = 1;    // signal tester: SIG_INTERVALS index (15 s)
  uint8_t sos_chan = 0xFF;     // SOS: channel index, 0xFF #emergency (SOS_CHAN_EMERGENCY)
  uint8_t mesh_time = 1;       // set the clock from mesh adverts while it isn't known
  uint8_t night_dnd = 0;       // do not disturb during the night hours (no alert chime / wake)
  uint8_t clock_face = 0;      // 0 digital, 1 analog, 2 big, 3 info
  uint16_t hidden_pages = 0;   // bit per home page (HomeScreen's order) not shown; the clock always is
  uint8_t power_save = 1;      // let the chip sleep while the screen is off (Settings > Sleep)
  uint8_t adv_nearby = 0;      // auto advert, zero hop: ADV_NEARBY_HOURS index (0 off)
  uint8_t adv_mesh = 0;        // auto advert, flood: ADV_MESH_HOURS index (0 off)
  uint8_t adv_page_mesh = 0;   // the Advert page's long press: 0 nearby, 1 whole mesh (flood)
  uint8_t usb_app = 0;         // the app protocol over USB too (SF32UsbInterface): PC apps, observer
  uint8_t rotation = 0;        // 0 normal, 1 upside down, 2 auto (by how it's held), 3 turned right, 4 left
  uint8_t glass = 0;           // theme style: 0 normal, 1 glass
  uint8_t glass_theme = 0;     // THEMES[] index used by the glass style (`theme`: the normal style's)
  uint8_t glass_bg = 0;        // glass background: 0 aurora, 1 dunes, 2 gradient, 3 none
  uint8_t batt_colors = 0;     // battery icon and % in red / yellow / green by charge
  uint8_t batt_icon = 1;       // battery icon beside the %: 0 = the % only
  uint8_t batt_graph = 3;      // Battery page graph: BATT_GRAPH_HOURS index (48 h)
  uint8_t batt_warn = 3;       // low battery alert: BATT_WARN_LEVELS index (20 %)
  uint8_t chg_limit = 0;       // charge to: CHG_LIMITS index (100 %)
  uint8_t bot_max = 2;         // bot: BOT_MAX_DAY index (20 replies a day)
  uint8_t bot_repeat = 1;      //   BOT_REPEAT_H index (once a day per sender)
  uint8_t bot_gap = 0;         //   BOT_GAP_MIN index (1 min)
  uint8_t tap_wake = 0;        // taps wake the dark screen (touch keeps scanning: ~60% more drain)
  uint8_t low_power = 0;       // low power mode: 0 off, 1 on (by hand), 2 on (auto, at the battery level below)
  uint8_t lp_auto = 0;         // low power mode on by itself at LP_AUTO_LEVELS[] % (0: never)
  uint8_t glass_light = 1;     // Glass: a glint of light that follows the board's tilt (motion sensor, screen on)
  uint8_t pet_sound = 1;       // the pet chirps when petted / played with (Settings > Games)
};

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  char _alert[80];
  char _banner_from[32], _banner_text[96];  // SF32: new message banner (showNewMessage)
  unsigned long _banner_expiry = 0, _banner_start = 0, _alert_start = 0;  // SF32: popups ease in and out
  bool _banner_first = false, _alert_first = false;
  bool _drag_on = false;  // SF32: a page following the finger (pageDrag)
  int _drag_dir = 0, _drag_off = 0;
  void pageDrag();
  float _hold_frac = 0;     // SF32: hold on the clock to change its face: the ring's progress
  bool _hold_done = false;  //   (the face changed during this hold)
  unsigned long _alert_expiry;
  int _msgcount;
  unsigned long ui_started_at, next_batt_chck;
  int next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  UIScreen* splash;
  UIScreen* home;
  UIScreen* msg_preview;
  UIScreen* curr;
  // SF32: menus
  UIScreen* menu = NULL;
  UIScreen* _menu_return = NULL;
  UIScreen* radio_edit = NULL;
  UIScreen* radio_field = NULL;
  UIScreen* theme_screen = NULL;
  UIScreen* about_screen = NULL;
  UIScreen* logs_screen = NULL;
  UIScreen* tz_screen = NULL;
  UIScreen* hour_screen = NULL;
  UIScreen* aod_screen = NULL;
  UIScreen* sos_screen = NULL;
  UIScreen* qr_screen = NULL;
  UIScreen* scan_screen = NULL;  // Radio > Band scan
  UIScreen* friends_screen = NULL;  // Starlit > Friends (ui_pet.inc)
  UIScreen* _scan_return = NULL;
  UIScreen* _qr_return = NULL;
  bool _qr_bright = false;  // brightness raised for the QR code
  // tap sounds play at the Sound volume (Low if chimes are off: they're their own setting)
  SF32Sound::Volume tapVolume() const { return _ui_prefs.sound ? (SF32Sound::Volume)_ui_prefs.sound : SF32Sound::VOL_LOW; }
  unsigned long _clock_reset_tap = 0;  // Reset clock: first tap (confirm with a second)
  bool _audio_open = false;  // audio path kept open for tap sounds (screen on)
  bool _startup_sound_done = false;
  unsigned long _tap_preview_at = 0;  // Settings > Tap sound: play the new style then
  unsigned long _audio_grace_until = 0;  // keep the audio path open until then (settings changed)
  unsigned long _screen_off_since = 0;   // when the screen went off / to always-on (0: in use)
  unsigned long _cpu_idle_since = 0;     // screen and audio off since (0: not idle)
  SF32Sound::Volume _audio_open_vol = SF32Sound::VOL_OFF;
  // SOS (ui_sos.inc)
  uint8_t _sos_state = 0, _sos_sent = 0, _sos_beeped = 0xFF;
  unsigned long _sos_t = 0;  // countdown start / next resend
  void createSosScreen();
  void sendSos();
  void sosLoop();
  UIScreen* _aod_return = NULL;
  bool _aod = false;
  void enterAOD();
  UIScreen* _hour_menu_return = NULL;
  unsigned long _next_night_check = 0;
  int _applied_brightness = -1;
  bool nightHours() const;
  bool nightNow() const;  // night dim active
  void applyBrightness();
  UIScreen* _tz_return = NULL;
  UIScreen* _tz_menu_return = NULL;
  int _tz_return_kind = 0;
  void goBack();
  UIScreen* _menu_return_theme = NULL;
  UIScreen* radio_review = NULL;
  unsigned long _display_off_at = 0;
  void resumeOrReset();
  void createEditors();
  SF32UIPrefs _ui_prefs;
  void loadUIPrefs();
  void saveUIPrefs();
  void handleTouchMenus();
  void screenOff();
  void showNewMessage(bool direct);
  bool _bt_restored = false;
  void powerCare();
  unsigned long _next_batt_warn_check = 0;
  uint8_t _batt_warn_level = 0;  // 0 none, 1 low shown/pending, 2 critical shown
  bool _lp_note_pending = false;  // low power mode came on by itself: say so when the screen is on
  bool _lp_declined = false;      // low power mode turned off by hand this discharge: no auto-on again
  bool _batt_warn_pending = false;
  bool _dimmed = false;
  bool _was_face_down = false;
  unsigned long _seen_auto_off = 0, _last_activity = 0;

  // Bot (ui_bot.inc): replies are queued from the receive callbacks and sent from loop()
  struct BotReply { bool pending; bool in_channel; unsigned long due; uint8_t pub_key[PUB_KEY_SIZE]; mesh::GroupChannel channel; char name[32]; };
  BotReply _bot_reply = {};
  struct BotSeen { uint32_t key; unsigned long at; };
  BotSeen _bot_seen[16] = {};
  unsigned long _bot_last_sent = 0;
  unsigned long _bot_day_start = 0;  // the 24 h the daily reply cap counts in (0: none yet)
  uint8_t _bot_day_n = 0;
  uint16_t _bot_count = 0;  // replies sent since boot
  char _bot_last[32] = "";  // who got the last one
  void botCheck(mesh::Packet* pkt, const ContactInfo* from, const ChannelDetails* chan, const char* text);
  void botLoop();

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() { setCurrScreen(home); }
  void showAlert(const char* text, int duration_millis);
  int  getMsgCount() const { return _msgcount; }
  int  getUnreadCount() const;  // SF32: messages not yet viewed on the device
  bool showMessages();          // SF32: open the unread messages (false if none)

  // SF32: menus and settings
  enum MenuKind { MENU_POWER, MENU_PAGE, MENU_SETTINGS, MENU_TIME, MENU_NIGHT, MENU_SOUND, MENU_DISPLAY, MENU_MOTION, MENU_PAGES, MENU_ADVERTS, MENU_THEME, MENU_BATTERY, MENU_BOT, MENU_POWER_OFF, MENU_LOW_POWER, MENU_GAMES, MENU_MUTE };
  // the menu a submenu goes back to (MENU_PAGE: not a submenu)
  static MenuKind menuParent(MenuKind k) {
    return (k == MENU_NIGHT || k == MENU_TIME || k == MENU_SOUND || k == MENU_DISPLAY || k == MENU_MOTION ||
            k == MENU_PAGES || k == MENU_ADVERTS || k == MENU_THEME || k == MENU_BATTERY || k == MENU_BOT ||
            k == MENU_LOW_POWER || k == MENU_GAMES) ? MENU_SETTINGS : MENU_PAGE;
  }
  void openMenu(MenuKind kind);
  void closeMenu();
  void menuAction(uint8_t action);
  const SF32UIPrefs& uiPrefs() const { return _ui_prefs; }
  // Low power mode (ui_power.inc): what the settings come to while it's on. The saved settings
  // are never changed, so turning it off puts everything back.
  bool lowPower() const { return _ui_prefs.low_power != 0; }
  uint8_t tapSnd() const { return lowPower() ? 0 : _ui_prefs.touch_snd; }
  bool botOn() const { return _ui_prefs.bot_on && !lowPower(); }
  bool motionWanted() const { return !lowPower() && (_ui_prefs.lift_wake || _ui_prefs.flip_quiet || _ui_prefs.rotation == 2); }
  bool _glass_light_live = false;  // the tilt light is running (Glass shown, screen on): needs the motion sensor
  void glassLightLoop();
  void petStroke();
  void applyMotion();
  void setLowPower(uint8_t mode);  // 0 off, 1 by hand, 2 auto
  void petSound(int kind);  // the pet's chirp: 1 petted, 2 playing (Settings > Games > Sounds)
  unsigned long autoOffMillis() const;
  int32_t localOffsetSecs(uint32_t utc) const;  // time zone + daylight saving
  void openRadioEditor();
  void leaveRadioEditor();
  void discardRadio();
  void openRadioField(int field);
  void closeRadioField();
  void openRadioReview(bool leaving);
  void closeRadioReview();
  void applyRadio();
  void setTheme(int i);  // the colour of the current style
  int themeIndex() const { return _ui_prefs.glass ? _ui_prefs.glass_theme : _ui_prefs.theme; }
  void applyStyle();     // the saved style and its colour
  void applyBattery();   // Settings > Battery: how the % shows, the charge limit
  void addBatteryRows(class MenuScreen& m);  // its rows (Settings > Battery and the Battery page menu)
  void openTheme();
  void closeTheme();
  void closeToMenu(MenuKind kind);  // from a screen opened from a menu, back to that menu
  void exitAOD();
  void openHour(bool until);
  void closeHour();
  void nightChanged();
  void openTimezone();
  void closeTimezone();
  void openLogs();
  void closeLogs();
  void openAbout();
  void closeAbout();
  bool isRadioScreen() const;
  bool dndNow() const;  // do not disturb active (night hours)
  bool radioHasChanges() const;
  void openScan();   // band scan (ui_scan.inc)
  void closeScan();
  void openFriends();  // the Starlits of the nodes heard (ui_pet.inc)
  void openNodeCard(const ContactInfo& c);  // one node's card (ui_pet.inc)
  void closeFriends();
  void openQr();   // share this node's contact as a QR code (ui_qr.inc)
  void closeQr();
  void openFlashlight();
  void applyRotation();  // ui_power.inc: Display > Rotation (auto: from the accelerometer)
  bool sendAdvert(bool mesh_wide);  // ui_adverts.inc: nearby (zero hop) or the whole mesh
  void advertLoop();                // scheduled adverts (Settings > Auto advert)
  uint8_t _adv_nearby_h = 0, _adv_mesh_h = 0;
  unsigned long _adv_nearby_at = 0, _adv_mesh_at = 0;  // power menu > Flashlight (ui_qr.inc: the QR screen, all lit)
  bool flashlightOn() const;
  void openSos();
  void stopSos();
  uint8_t sosState() const { return _sos_state; }
  uint8_t sosSent() const { return _sos_sent; }
  int sosSecondsLeft() const;
  uint16_t botCount() const { return _bot_count; }
  const char* botLastName() const { return _bot_last; }
private:
  MenuKind _menu_kind = MENU_PAGE;
  // where Back goes: a Settings submenu can also be opened from a page's menu (its "... menu"
  // row), Power from a page (swipe down) or Settings, Reboot / off from Power or the Power page
  MenuKind _sub_from = MENU_SETTINGS, _power_from = MENU_PAGE, _poff_from = MENU_POWER;
  int _back_sel = 0;                    // the parent's row that opened this submenu
  uint8_t _mute_rows[20];               // Settings > Sound > Channels: each row's channel index
  int _mute_n = 0;
  int _mute_back_sel = 0;               //   and Settings' row while it's open
  MenuKind _left_kind = MENU_PAGE;      // the menu a screen (Logs, Color, pickers) was opened from
  int _left_sel = 0;                    //   and its row, to come back to
  MenuKind parentOf(MenuKind k) const {
    if (k == MENU_POWER) return _power_from;
    if (k == MENU_POWER_OFF) return _poff_from;
    if (k == MENU_MUTE) return MENU_SOUND;
    if (menuParent(k) == MENU_SETTINGS) return _sub_from;
    return menuParent(k);
  }
  int _menu_open_dir = 0;  // SF32Display::Transition the open menu came in with
public:
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;

  bool isBuzzerQuiet() { 
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int offline_queue_size) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onControlDataRecv(const mesh::Packet* pkt) override;
  bool onUnhandledResponse(const ContactInfo &from, uint32_t tag, const uint8_t* data, uint8_t len) override;  // SF32: My Nodes

  // from AbstractUITask
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
