// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Muted channels (Settings > Sound > Channels): a message on a muted channel makes no sound,
// doesn't light the screen, shows no banner and isn't counted as unread on the board. The
// phone still gets it. The phone app's own mute stays on the phone (the companion protocol
// doesn't carry it), so the board keeps its own list in /sf32_mute.
// A channel is known by the start of its secret, so renaming or reordering channels in the
// app keeps it muted; a deleted channel's entry is dropped the next time the list changes.

#define MUTE_FILE  "/sf32_mute"
#define MUTE_MAGIC 0x53464D31  // "SFM1"
#define MUTE_MAX   MAX_GROUP_CHANNELS

class ChannelMutes {
  struct { uint32_t magic; uint8_t n, pad[3]; uint8_t key[MUTE_MAX][4]; } _d;

  int find(const mesh::GroupChannel& ch) const {
    for (int i = 0; i < _d.n; i++) if (memcmp(_d.key[i], ch.secret, 4) == 0) return i;
    return -1;
  }
  void save() {
    File f = InternalFS.open(MUTE_FILE, "w", true);
    if (f) f.write((const uint8_t*)&_d, sizeof(_d));
  }

public:
  void begin() {
    memset(&_d, 0, sizeof(_d));
    File f = InternalFS.open(MUTE_FILE, "r");
    if (!f || f.read((uint8_t*)&_d, sizeof(_d)) != (int)sizeof(_d) || _d.magic != MUTE_MAGIC || _d.n > MUTE_MAX) {
      memset(&_d, 0, sizeof(_d));
    }
    _d.magic = MUTE_MAGIC;
  }
  bool muted(const mesh::GroupChannel& ch) const { return find(ch) >= 0; }
  // muted channels that still exist (the menu's count)
  int count() {
    int n = 0;
    ChannelDetails cd;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
      if (the_mesh.getChannel(i, cd) && cd.name[0] && muted(cd.channel)) n++;
    return n;
  }
  void toggle(const mesh::GroupChannel& ch) {
    int i = find(ch);
    if (i >= 0) {
      memmove(_d.key[i], _d.key[i + 1], (_d.n - i - 1) * 4);
      _d.n--;
    } else {
      // keep only channels that still exist, so deleted ones don't fill the list
      uint8_t keep[MUTE_MAX][4];
      int n = 0;
      ChannelDetails cd;
      for (int c = 0; c < MAX_GROUP_CHANNELS && n < MUTE_MAX; c++)
        if (the_mesh.getChannel(c, cd) && cd.name[0] && muted(cd.channel)) memcpy(keep[n++], cd.channel.secret, 4);
      memcpy(_d.key, keep, n * 4);
      _d.n = n;
      if (_d.n < MUTE_MAX) memcpy(_d.key[_d.n++], ch.secret, 4);
    }
    save();
  }
};
static ChannelMutes channel_mutes;
