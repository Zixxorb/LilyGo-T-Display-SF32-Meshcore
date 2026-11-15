// Build-time defaults for the T-Display-SF32 firmwares (the equivalent of a
// MeshCore variant's platformio.ini build_flags). Everything here can be changed
// at runtime via the CLI / config.meshcore.io and is then saved in /prefs.json.
#pragma once

// Vesper's own version (shown on the start screen and About; the release tag must match:
// .github/workflows/release.yml checks). MeshCore's own version stays what the apps see.
#define VESPER_VERSION "1.1.0"

// MeshCore "USA/Canada" preset
#define LORA_FREQ      910.525
#define LORA_BW        62.5
#define LORA_SF        7
#define LORA_CR        5
#define LORA_TX_POWER  22   // dBm, SX1262 maximum

#ifdef SF32_COMPANION
  // Companion firmware (MeshCore's examples/companion_radio)
  #define MAX_CONTACTS        350
  #define MAX_GROUP_CHANNELS  40
  #define OFFLINE_QUEUE_SIZE  256
  // No ENABLE_USB_INTERFACE: MeshCore counts a USB interface as always connected, which
  // hides the BLE PIN on screen (stock BLE companion builds leave it out too).
  #define BLE_PIN_CODE        123456  // with a display: random PIN each boot, shown on screen
  // Touchscreen (SF32TouchInput): taps act like button A, horizontal swipes change page
  #define PIN_USER_BTN_ANA    0x7F    // TOUCH_VIRTUAL_PIN
  #define UI_HAS_ROTARY_INPUT 1
#elif defined(SF32_ROOM_SERVER)
  // Room server firmware (MeshCore's examples/simple_room_server)
  #define ADVERT_NAME    "SF32 Room"
  #define ADVERT_LAT     0.0
  #define ADVERT_LON     0.0
  #define ADMIN_PASSWORD "password"
  #define ROOM_PASSWORD  "hello"
#else
  // Repeater firmware (MeshCore's examples/simple_repeater)
  #define ADVERT_NAME    "SF32 Repeater"
  #define ADVERT_LAT     0.0
  #define ADVERT_LON     0.0
  #define ADMIN_PASSWORD "password"
  #define MAX_NEIGHBOURS 50
#endif

// Display + user button (MeshCore's simple_repeater UITask)
#define DISPLAY_CLASS  SF32Display
#define PIN_USER_BTN   34   // button "A" (PA34, active high): click wakes the screen, hold = power off (power on: D key or USB)
