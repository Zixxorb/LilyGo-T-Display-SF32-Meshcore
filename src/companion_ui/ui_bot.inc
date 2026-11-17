// Part of UITask.cpp (#included there, in this order; not compiled on its own).
// Bot: auto-reply to messages sent with 1-byte path hashes, asking the sender to switch
// to 2-byte. Its own page in the main pages (tap: on/off; swipe up: options). Off by default.
//
// A flood packet's path_len carries the hash size its sender chose (bits 6-7, see
// MeshCore docs/packet_format.md), and repeaters keep it, so Packet::getPathHashSize()
// on a received flood message is the sender's setting. Direct-routed packets follow a
// stored path instead, so they are skipped.
//
// Etiquette (Settings > Bot): each sender gets the reminder at most once a day (RAM table,
// cleared at boot), at most one reply a minute overall and 20 a day in all (the defaults) (channel sender names
// can be made up, so the per-sender table alone can't stop a flood of replies), and
// messages that look like auto-replies are never answered (two bots can't ping-pong).

// the limits are Settings > Bot (Daily max, Repeat, Gap); the daily count runs over 24 h
#define BOT_DAY_MS              (24UL * 60 * 60 * 1000)
#define BOT_SENDER_COOLDOWN_MS  (BOT_REPEAT_H[_ui_prefs.bot_repeat % 3] * 3600000UL)
#define BOT_MIN_GAP_MS          (BOT_GAP_MIN[_ui_prefs.bot_gap % 3] * 60000UL)
#define BOT_MAX_PER_DAY         (BOT_MAX_DAY[_ui_prefs.bot_max % 4])
#define BOT_REPLY_DELAY_MS      3000UL  // let the incoming flood and its ACK clear first

static const char BOT_DM_TEXT[] =
  "Auto-reply: your node sends 1-byte path hashes. Please switch to 2-byte "
  "(path hash mode in your settings; update firmware if it isn't there).";
static const char BOT_CHAN_TEXT[] =
  "auto-reply: you're on 1-byte path hashes, please switch to 2-byte.";

static uint32_t botKey(const uint8_t* data, int len) {  // FNV-1a
  uint32_t h = 2166136261u;
  for (int i = 0; i < len; i++) h = (h ^ data[i]) * 16777619u;
  return h ? h : 1;
}

static bool botLooksAutomatic(const char* text) {
  const char* p = strstr(text, "uto-reply");  // "Auto-reply" / "auto-reply"
  return p != NULL && p > text && (p[-1] == 'A' || p[-1] == 'a');
}

void UITask::botCheck(mesh::Packet* pkt, const ContactInfo* from, const ChannelDetails* chan, const char* text) {
  if (!botOn() || pkt == NULL || !pkt->isRouteFlood()) return;
  if (pkt->getPathHashSize() != 1) return;
  if (_bot_reply.pending) return;  // one at a time
  if (_bot_last_sent && millis() - _bot_last_sent < BOT_MIN_GAP_MS) return;
  if (_bot_day_start && millis() - _bot_day_start >= BOT_DAY_MS) _bot_day_start = 0;  // a new day
  if (_bot_day_start && _bot_day_n >= BOT_MAX_PER_DAY) return;  // today's replies used up

  BotReply r = {};
  const char* body = text;
  if (from) {
    if (!_ui_prefs.bot_dm) return;
    memcpy(r.pub_key, from->id.pub_key, PUB_KEY_SIZE);
    StrHelper::strncpy(r.name, from->name, sizeof(r.name));
  } else {
    if (_ui_prefs.bot_chan == 0 || chan == NULL) return;
    // channel text is "<sender>: <message>"
    const char* sep = strstr(text, ": ");
    if (sep == NULL || sep == text || sep - text >= (int)sizeof(r.name)) return;
    memcpy(r.name, text, sep - text);
    r.name[sep - text] = 0;
    // it's quoted back in "@[name]": no brackets or control characters in it
    for (char* p = r.name; *p; p++) if (*p == '[' || *p == ']' || (unsigned char)*p < 0x20) *p = '_';
    body = sep + 2;
    if (_ui_prefs.bot_chan == 1) {  // DM: only to a known contact with exactly that name
      ContactInfo* c = the_mesh.searchContactsByPrefix(r.name);
      if (c == NULL || strcmp(c->name, r.name) != 0) return;
      memcpy(r.pub_key, c->id.pub_key, PUB_KEY_SIZE);
    } else {
      r.in_channel = true;
      r.channel = chan->channel;
      StrHelper::strncpy(r.chan_name, chan->name, sizeof(r.chan_name));
    }
  }
  if (botLooksAutomatic(body)) return;

  // once a day per sender (by public key, or by name for channel replies)
  uint32_t key = r.in_channel ? botKey((const uint8_t*)r.name, strlen(r.name)) : botKey(r.pub_key, PUB_KEY_SIZE);
  unsigned long now = millis();
  int slot = 0;
  for (int i = 0; i < (int)(sizeof(_bot_seen) / sizeof(_bot_seen[0])); i++) {
    if (_bot_seen[i].key == key) {
      if (now - _bot_seen[i].at < BOT_SENDER_COOLDOWN_MS) return;
      slot = i;
      break;
    }
    if (_bot_seen[i].key == 0 || _bot_seen[i].at < _bot_seen[slot].at) slot = i;  // free or oldest
  }
  _bot_seen[slot].key = key;
  _bot_seen[slot].at = now;

  r.pending = true;
  r.due = now + BOT_REPLY_DELAY_MS;
  _bot_reply = r;
}

void UITask::botLoop() {
  if (!_bot_reply.pending || (long)(millis() - _bot_reply.due) < 0) return;
  _bot_reply.pending = false;
  if (!botOn()) return;  // turned off (or low power mode) while waiting

  uint32_t ts = the_mesh.getRTCClock()->getCurrentTimeUnique();
  bool ok = false;
  if (_bot_reply.in_channel) {
    char text[MAX_TEXT_LEN + 1];
    snprintf(text, sizeof(text), "@[%s] %s", _bot_reply.name, BOT_CHAN_TEXT);
    echo_tracker.tagNext(ECHO_TAG_BOT);  // listen for a repeater passing it on (ui_echo.inc)
    ok = the_mesh.sendGroupMessage(ts, _bot_reply.channel, the_mesh.getNodeName(), text, strlen(text));
  } else {
    ContactInfo* c = the_mesh.lookupContactByPubKey(_bot_reply.pub_key, PUB_KEY_SIZE);
    if (c) {
      uint32_t expected_ack, est_timeout;
      ok = the_mesh.sendMessage(*c, ts, 0, BOT_DM_TEXT, expected_ack, est_timeout) != MSG_SEND_FAILED;
    }
  }
  if (ok) {  // remember it was nudged (its Starlit shows it; an upgrade later shows in today's finds)
    ContactInfo* c = the_mesh.lookupContactByPubKey(_bot_reply.pub_key, PUB_KEY_SIZE);
    if (!c && _bot_reply.in_channel) {  // a channel reply: by name, if it's a contact
      c = the_mesh.searchContactsByPrefix(_bot_reply.name);
      if (c && strcmp(c->name, _bot_reply.name) != 0) c = NULL;
    }
    if (c) hash_sizes.setNudged(c->id.pub_key);
  }
  _bot_last_sent = millis();
  if (!_bot_day_start) { _bot_day_start = millis() | 1; _bot_day_n = 0; }
  _bot_day_n++;
  if (ok) {
    eventSound(SF32Sound::CHIME_BOT);
    _bot_count++;
    StrHelper::strncpy(_bot_last, _bot_reply.name, sizeof(_bot_last));
    _next_refresh = 0;  // the Bot page shows it
  }

  if (ok && _bot_reply.in_channel) {  // logged once it's known whether a repeater passed it on
    _bot_last_heard = -1;
    _bot_log_pending = true;
    StrHelper::strncpy(_bot_log_chan, _bot_reply.chan_name, sizeof(_bot_log_chan));
    return;
  }
  if (!ok) echo_tracker.tagNext(0);  // nothing went out to follow
  if (ok) _bot_last_heard = -2;
  char line[48];
  snprintf(line, sizeof(line), "Bot %s %s%s", ok ? "replied:" : "failed:", _bot_reply.name, ok ? " (DM)" : "");
  board.logEvent(line);
}

// The bot's channel reply: once the tracker is done listening, log it with the repeats heard
// ("heard 2x": repeaters passed it on; "not heard": none in range heard it, or we missed them)
void UITask::botEchoLoop() {
  if (!_bot_log_pending) return;
  uint8_t heard, tries;
  char line[64];
  if (!echo_tracker.takeResult(ECHO_TAG_BOT, heard, tries)) {
    if (millis() - _bot_last_sent < 120000UL) return;  // still listening
    _bot_log_pending = false;  // never followed (it didn't go out, or something else went first)
    _bot_last_heard = -2;
    snprintf(line, sizeof(line), "Bot replied: %s in %s", _bot_last, _bot_log_chan);
    board.logEvent(line);
    return;
  }
  _bot_log_pending = false;
  _bot_last_heard = heard;
  _next_refresh = 0;  // the Bot page shows it
  if (heard) snprintf(line, sizeof(line), "Bot (heard %ux): %s in %s", heard, _bot_last, _bot_log_chan);
  else if (tries) snprintf(line, sizeof(line), "Bot (not heard, resent %ux): %s in %s", tries, _bot_last, _bot_log_chan);
  else snprintf(line, sizeof(line), "Bot (not heard): %s in %s", _bot_last, _bot_log_chan);
  board.logEvent(line);
}
