// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Sound theme, message chime choices and event sounds (Settings > Sound), kept in /sf32_snd
// (their own file: the settings file stays as it is). The sounds themselves are recipes in
// SF32Sound.cpp; this picks them and plays the events.

#define SND_FILE  "/sf32_snd"
#define SND_MAGIC 0x444E5346  // "FSND"

void UITask::loadSoundPrefs() {
  struct { uint32_t magic; uint8_t theme, direct, channel, events; } f = {};
  File file = InternalFS.open(SND_FILE, "r");
  if (file && file.read((uint8_t*)&f, sizeof(f)) == (int)sizeof(f) && f.magic == SND_MAGIC) {
    _snd_theme = f.theme < SF32Sound::THEMES ? f.theme : 0;
    _snd_direct = f.direct <= SF32Sound::CHIMES ? f.direct : 0;
    _snd_channel = f.channel <= SF32Sound::CHIMES ? f.channel : 0;
    _snd_events = f.events ? 1 : 0;
  }
  sound.setTheme(_snd_theme, _snd_direct, _snd_channel);
}

void UITask::saveSoundPrefs() {
  struct { uint32_t magic; uint8_t theme, direct, channel, events; } f = {SND_MAGIC, _snd_theme, _snd_direct, _snd_channel, _snd_events};
  File file = InternalFS.open(SND_FILE, "w", true);
  if (file) file.write((const uint8_t*)&f, sizeof(f));
  sound.setTheme(_snd_theme, _snd_direct, _snd_channel);
}

// an event's sound: at the Sound volume, only with Events on, and never when it should be
// quiet (do not disturb, lying face down, low power mode)
void UITask::eventSound(SF32Sound::Chime kind) {
  if (!_snd_events || !_ui_prefs.sound || lowPower() || dndNow()) return;
  if (_ui_prefs.flip_quiet && motion.faceDown()) return;
  sound.chime(kind, (SF32Sound::Volume)_ui_prefs.sound);
}

// changes that make a sound: the phone connecting or going, the charger in or out
void UITask::eventLoop() {
  static int8_t link = -1, usb = -1;
  static unsigned long usb_at = 0, link_at = 0;
  bool l = hasConnection();
  if (link < 0) link = l;
  else if (l != (bool)link) {  // held 12 s: a phone app dropping and coming back (backgrounded) stays quiet
    if (!link_at) link_at = millis() | 1;
    if (millis() - link_at > 12000) {
      link = l;
      link_at = 0;
      eventSound(l ? SF32Sound::CHIME_CONNECT : SF32Sound::CHIME_DISCONNECT);
    }
  } else link_at = 0;
  bool u = board.isExternalPowered();
  if (usb < 0) usb = u;
  else if (u != (bool)usb) {
    if (!usb_at) usb_at = millis() | 1;
    if (millis() - usb_at > 1500) {  // (held 1.5 s: a wobbly cable doesn't chirp on every blip)
      usb = u;
      usb_at = 0;
      eventSound(u ? SF32Sound::CHIME_PLUG : SF32Sound::CHIME_UNPLUG);
    }
  } else usb_at = 0;
}
