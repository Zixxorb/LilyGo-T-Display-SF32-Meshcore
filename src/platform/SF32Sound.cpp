#include "SF32Sound.h"

#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <math.h>
#include <string.h>
#include <drivers/audio.h>
#include "bf0_hal.h"

extern "C" void sf32_cpu_full_speed();  // SF32Board.cpp: audio needs the full clock
#define LOG_TAG "sound"
#include <ulog.h>

#define AMP_PIN      20        // PA20: NS4150B enable (LilyGo: NS4150B_CTRL)
#define SAMPLE_RATE  16000
#define CHUNK        320       // bytes per DMA half buffer (10 ms of 16-bit mono)

SF32Sound sound;

struct Note { uint16_t hz, ms; };
// a rising two-note chime for direct messages, one short note for channels
static Note CHIME_NOTES[][3] = {
  {{1319, 90}, {1760, 160}, {0, 0}},  // direct: E6, A6
  {{1568, 110}, {0, 0}, {0, 0}},      // channel: G6
  {{1047, 90}, {1319, 90}, {1568, 160}},  // test: C6 E6 G6
  {{0, 0}, {0, 0}, {0, 0}},               // custom: set by tone()
  // touch feedback
  {{1600, 35}, {0, 0}, {0, 0}},           // click
  {{1000, 60}, {0, 0}, {0, 0}},           // pop: lower, longer
  {{2400, 12}, {0, 0}, {0, 0}},           // tick: very short
  {{0, 0}, {0, 0}, {0, 0}},               // swish: synthesised (fill_tap)
  {{659, 110}, {988, 110}, {1319, 380}},  // startup: E5 B5 E6, each ringing out
};

static rt_device_t s_prc, s_codec;
static struct rt_event s_ev;
static struct rt_mailbox s_mb;
static rt_ubase_t s_mb_pool[2];
static volatile bool s_busy = false;
static volatile uint8_t s_cur_kind = 0;  // what's playing while s_busy
static bool s_started = false;

static rt_err_t on_tx_done(rt_device_t, void*) {
  rt_event_send(&s_ev, 1);
  return RT_EOK;
}

static bool open_devices() {
  if (s_prc) return true;
  s_prc = rt_device_find("audprc");
  s_codec = rt_device_find("audcodec");
  if (!s_prc || !s_codec) { LOG_E("audio devices not found"); s_prc = s_codec = nullptr; return false; }
  if (rt_device_open(s_prc, RT_DEVICE_FLAG_RDWR) != RT_EOK ||
      rt_device_open(s_codec, RT_DEVICE_FLAG_WRONLY) != RT_EOK) {
    LOG_E("audio devices won't open");
    s_prc = s_codec = nullptr;
    return false;
  }
  rt_pin_mode(AMP_PIN, PIN_MODE_OUTPUT);
  rt_pin_write(AMP_PIN, PIN_LOW);
  return true;
}

// Same configuration as LilyGo's audprc example (mono, mixed to the speaker).
static void config_tx(int volume) {
  struct rt_audio_caps caps;
  struct rt_audio_sr_convert cfg;
  rt_device_control(s_codec, AUDIO_CTL_SETOUTPUT, (void*)AUDPRC_TX_TO_CODEC);
  caps.main_type = AUDIO_TYPE_OUTPUT;
  caps.sub_type = 1 << HAL_AUDCODEC_DAC_CH0;
  caps.udata.config.channels = 1;
  caps.udata.config.samplerate = SAMPLE_RATE;
  caps.udata.config.samplefmt = 16;
  rt_device_control(s_codec, AUDIO_CTL_CONFIGURE, &caps);

  rt_device_control(s_prc, AUDIO_CTL_SETOUTPUT, (void*)AUDPRC_TX_TO_CODEC);
  cfg.channel = 1;
  cfg.source_sr = SAMPLE_RATE;
  cfg.dest_sr = SAMPLE_RATE;
  rt_device_control(s_prc, AUDIO_CTL_OUTPUTSRC, (void*)&cfg);
  caps.main_type = AUDIO_TYPE_SELECTOR;
  caps.sub_type = 0xFF;
  caps.udata.value = 0x5050;
  rt_device_control(s_prc, AUDIO_CTL_CONFIGURE, &caps);
  caps.main_type = AUDIO_TYPE_MIXER;
  rt_device_control(s_prc, AUDIO_CTL_CONFIGURE, &caps);
  caps.main_type = AUDIO_TYPE_OUTPUT;
  caps.sub_type = HAL_AUDPRC_TX_CH0;
  caps.udata.config.channels = 1;
  caps.udata.config.samplerate = SAMPLE_RATE;
  caps.udata.config.samplefmt = 16;
  rt_device_control(s_prc, AUDIO_CTL_CONFIGURE, &caps);
  rt_device_control(s_codec, AUDIO_CTL_SETVOLUME, (void*)(rt_base_t)volume);  // half-dB steps
}

static void stream(int cmd) {
  int codec = AUDIO_STREAM_REPLAY | ((1 << HAL_AUDCODEC_DAC_CH0) << 8);
  int prc = AUDIO_STREAM_REPLAY | ((1 << HAL_AUDPRC_TX_CH0) << 8);
  rt_device_control(s_codec, cmd, &codec);
  if (cmd == AUDIO_CTL_START) rt_device_set_tx_complete(s_prc, on_tx_done);
  rt_device_control(s_prc, cmd, &prc);
}

// one 10 ms chunk of the sequence starting at sample n; false after the end
static bool fill(int16_t* buf, const Note* notes, uint32_t n) {
  const int count = CHUNK / 2;
  bool any = false;
  for (int i = 0; i < count; i++, n++) {
    uint32_t t = n, start = 0;
    float v = 0;
    for (int k = 0; k < 3 && notes[k].hz; k++) {
      uint32_t len = (uint32_t)notes[k].ms * SAMPLE_RATE / 1000;
      if (t < start + len) {
        float x = (float)(t - start) / SAMPLE_RATE;
        if (notes[k].hz == 1) { any = true; break; }  // a gap
        float env = expf(-x * 18.0f) * fminf(1.0f, (t - start) / 48.0f);  // bell: soft attack, decay
        if (notes[k].ms <= 15) env = fminf(1.0f, (len - (t - start)) / 24.0f) * fminf(1.0f, (t - start) / 8.0f);  // tick
        v = env * sinf(2 * (float)M_PI * notes[k].hz * x);
        any = true;
        break;
      }
      start += len;
    }
    buf[i] = (int16_t)(v * 14000);
  }
  return any;
}


// Opening the audio path uses the sequence that has always worked here: codec DAC muted,
// stream started, amplifier on, 30 ms, unmute (the DAC then ramps its volume up over the
// first ~100+ ms: a chime outlasts that, a 12-60 ms tap sound doesn't). So while the UI
// asks for it (screen on, tap sounds on) the path stays open, playing silence, and each
// sound starts at once at full level. Otherwise it opens for a sound and closes after.
#define MSG_OPEN   0xFE  // keep-open request: start the path without a sound
#define MSG_NONE   0xFFFF
// Power: the SDK leaves the codec's PLL, reference and bandgap on after the first sound,
// and the crystal's audio buffer on from boot (~2 mA, 2026-11-09 night test). With the
// screen off, they go off whenever the path closes; the next sound turns them on first.
#define MSG_POWER_DOWN 0xFD  // a nudge: power down now if the path is closed
extern "C" void bf0_disable_pll(void);
static volatile bool s_power_save = false, s_down = false;
static void audio_power_down() {
  bf0_disable_pll();       // PLL, refgen, bandgap off; the next stream start turns them on again
  HAL_PMU_EnableAudio(0);  // the crystal's buffer to the codec
  s_down = true;
}
static void audio_power_up() {
  HAL_PMU_EnableAudio(1);
  s_down = false;
}
static volatile bool s_keep_open = false, s_running = false;
static volatile int s_step = 0;  // diagnostics: where the sound thread is
static volatile uint32_t s_open_samples = 0;  // samples played since the path last opened (unmuted)

static int volume_half_db(SF32Sound::Volume vol) {
  // half-dB: Low -24 dB, Medium -6 dB, High 0 dB (the speaker is efficient: louder was too loud)
  return vol == SF32Sound::VOL_LOW ? -48 : (vol == SF32Sound::VOL_MEDIUM ? -12 : 0);
}

// Tap sounds are synthesised (a plain beep slice sounded harsh): a quick attack, a natural
// decay and a little pitch movement. style 1 click: a soft wooden "tock"; 2 pop: a water-
// drop "bloop" (pitch rising); 3 tick: a crisp, quiet mechanical tick. False after the end.
static bool fill_tap(int16_t* buf, int style, uint32_t n) {
  if (style == 4) {  // swish (swipes): airy filtered noise that swells and fades, a whoosh
    const float T = 0.14f;
    static uint32_t rng;
    static float lp1, lp2;
    if (n == 0) { rng = 0x1234567; lp1 = lp2 = 0; }
    const float a2 = 1 - expf(-2 * (float)M_PI * 1500 / SAMPLE_RATE);  // high-pass: no thud
    bool any = false;
    for (int i = 0; i < CHUNK / 2; i++) {
      float t = (float)(n + i) / SAMPLE_RATE, v = 0;
      if (t < T) {
        any = true;
        rng = rng * 1664525u + 1013904223u;
        float noise = (float)(rng >> 8) / 8388608.0f - 1.0f;
        float fc = 3000 + 4000 * (t / T);  // low-pass rising 3 -> 7 kHz: brightens as it moves
        lp1 += (1 - expf(-2 * (float)M_PI * fc / SAMPLE_RATE)) * (noise - lp1);
        lp2 += a2 * (lp1 - lp2);
        float s = sinf((float)M_PI * t / T);
        v = 0.85f * s * s * (lp1 - lp2);  // smooth swell, no attack transient (softer than taps)
        v = v > 1 ? 1 : (v < -1 ? -1 : v);
      }
      buf[i] = (int16_t)(v * 16000);
    }
    return any;
  }
  float T, f0, f1, tau, amp;
  switch (style) {
    case 1:  T = 0.030f; f0 = 950;  f1 = 650;  tau = 0.006f;  amp = 0.80f; break;
    case 2:  T = 0.045f; f0 = 380;  f1 = 950;  tau = 0;       amp = 0.85f; break;  // tau 0: sine window
    default: T = 0.010f; f0 = 3200; f1 = 3200; tau = 0.0015f; amp = 0.50f; break;
  }
  bool any = false;
  for (int i = 0; i < CHUNK / 2; i++) {
    float t = (float)(n + i) / SAMPLE_RATE;
    float v = 0;
    if (t < T) {
      any = true;
      float env = tau > 0 ? fminf(1.0f, t / 0.0015f) * expf(-t / tau) : sinf((float)M_PI * t / T);
      float phase = 2 * (float)M_PI * (f0 * t + (f1 - f0) * t * t / (2 * T));  // linear sweep
      v = amp * env * sinf(phase);
    }
    buf[i] = (int16_t)(v * 16000);
  }
  return any;
}

static void sound_thread(void*) {
  static int16_t bufs[2][CHUNK / 2];  // alternating: the device queue keeps a pointer, not a copy
  int16_t* buf = bufs[0];
  uint32_t since_start = 0;  // samples since the stream started
  const Note* notes = NULL;  // the sound playing, if any
  int tap_style = 0;         // or a synthesised tap sound (1..3)
  uint32_t snd_n = 0, snd_delay = 0;
  int cur_volume = 0;
  rt_tick_t idle_since = 0;
  while (1) {
    rt_ubase_t msg = MSG_NONE;
    s_step = 1;
    if (!s_running && s_power_save && !s_down) audio_power_down();
    if (!s_running) {
      if (rt_mb_recv(&s_mb, &msg, RT_WAITING_FOREVER) != RT_EOK) continue;
    } else {
      rt_mb_recv(&s_mb, &msg, 0);
    }
    if (msg != MSG_NONE && (msg & 0xFF) == MSG_POWER_DOWN) {
      if (!s_running) continue;  // closed: power down at the top (not the stream's tx-done wait)
      msg = MSG_NONE;            // open: at the top again once it closes
    }
    if (msg != MSG_NONE) {
      int kind = msg & 0xFF, volume = (int8_t)((msg >> 8) & 0xFF);
      if (!s_running) {
        sf32_cpu_full_speed();
#ifdef RT_USING_PM
        rt_pm_request(PM_SLEEP_MODE_IDLE);  // no light sleep while the audio path is open
#endif                                       // (its start-up waits included)
        if (s_down) audio_power_up();
        if (!open_devices()) {
          s_busy = false;
#ifdef RT_USING_PM
          rt_pm_release(PM_SLEEP_MODE_IDLE);
#endif
          continue;
        }
        config_tx(volume);
        cur_volume = volume;
        // muted DAC started and settled before the amplifier comes on, then unmute: the
        // gentlest order for the speaker (the old one had 0 ms between stream and amplifier)
        rt_device_control(s_codec, AUDIO_CTL_MUTE, (void*)1);
        rt_event_control(&s_ev, RT_IPC_CMD_RESET, RT_NULL);
        stream(AUDIO_CTL_START);
        rt_thread_mdelay(200);  // the DAC output rises to mid-rail; let the amplifier's input settle
        rt_pin_write(AMP_PIN, PIN_HIGH);  // NS4150B on (mode 1: a single rising edge)
        rt_thread_mdelay(40);
        rt_device_control(s_codec, AUDIO_CTL_MUTE, (void*)0);
        s_running = true;
        since_start = 0;
        idle_since = rt_tick_get();
      } else if (kind != MSG_OPEN && volume != cur_volume) {
        rt_device_control(s_codec, AUDIO_CTL_SETVOLUME, (void*)(rt_base_t)volume);
        cur_volume = volume;
      }
      if (kind != MSG_OPEN) {
        notes = CHIME_NOTES[kind];
        tap_style = kind >= SF32Sound::CHIME_CLICK && kind <= SF32Sound::CHIME_SWISH ? kind - SF32Sound::CHIME_CLICK + 1 : 0;
        snd_n = 0;
        snd_delay = 0;
#ifdef SF32_SOUND_LOG
        rt_kprintf("sound: kind %d volume %d (path %s)\n", kind, volume, since_start ? "open" : "starting");
#endif
      }
    }

    rt_uint32_t ev;
    s_step = 2;
    if (rt_event_recv(&s_ev, 1, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR, rt_tick_from_millisecond(200), &ev) != RT_EOK) {
      LOG_W("no tx-done from audprc");
#ifdef SF32_SOUND_LOG
      rt_kprintf("sound: no tx-done, path closed\n");
#endif
      notes = NULL;
      s_busy = false;
      rt_device_control(s_codec, AUDIO_CTL_MUTE, (void*)1);
      rt_pin_write(AMP_PIN, PIN_LOW);
      stream(AUDIO_CTL_STOP);
      s_running = false;
#ifdef RT_USING_PM
      rt_pm_release(PM_SLEEP_MODE_IDLE);
#endif
      continue;
    }
    buf = bufs[buf == bufs[0] ? 1 : 0];
    if (notes && snd_delay > 0) {
      memset(buf, 0, CHUNK);
      snd_delay = snd_delay > CHUNK / 2 ? snd_delay - CHUNK / 2 : 0;
    } else if (notes) {
      if (!(tap_style ? fill_tap(buf, tap_style, snd_n) : fill(buf, notes, snd_n))) {  // finished
        notes = NULL;
        s_busy = false;
#ifdef SF32_SOUND_LOG
        rt_kprintf("sound: finished\n");
#endif
        idle_since = rt_tick_get();
      }
      snd_n += CHUNK / 2;
    } else {
      memset(buf, 0, CHUNK);
    }
    // Two ways rt_device_write() (RT-Thread's audio layer) can hang this thread for good,
    // leaving it "busy" so every later sound is skipped:
    //  - the frame queue is full: it waits for room forever. Skip the write then; if the
    //    stream has really stalled, the tx-done timeout above closes the path.
    //  - with the device idle it sends the frame itself, then pops it waiting forever; if
    //    the DMA interrupt comes in between, the interrupt sends and pops it first, and the
    //    queue is empty for good. Flash writes (saving a setting) shift the timing enough to
    //    hit that. So write with interrupts off: a few us, and nothing in it blocks (not full).
    struct rt_audio_device* ad = (struct rt_audio_device*)s_prc;
    bool full = ad->replay && (rt_uint16_t)(ad->replay->queue.put_index - ad->replay->queue.get_index) >= ad->replay->queue.size;
    s_step = 3;
    if (!full) {
      rt_base_t level = rt_hw_interrupt_disable();
      rt_device_write(s_prc, 0, buf, CHUNK);
      rt_hw_interrupt_enable(level);
    }
#ifdef SF32_SOUND_LOG
    else rt_kprintf("sound: queue full, chunk skipped\n");
#endif
    s_step = 4;
    if (since_start < 0x7FFFFFFF) since_start += CHUNK / 2;
    s_open_samples = since_start;
    // close the path when nothing plays and nobody wants it open (after the end settles)
    if (!notes && !s_keep_open && rt_tick_get() - idle_since > rt_tick_from_millisecond(60)) {
      // the reverse: mute, amplifier off, then stop the DAC (it can't pop the speaker then)
      s_step = 5;
      rt_device_control(s_codec, AUDIO_CTL_MUTE, (void*)1);
      rt_thread_mdelay(20);
      rt_pin_write(AMP_PIN, PIN_LOW);
      rt_thread_mdelay(5);
      stream(AUDIO_CTL_STOP);
      s_running = false;
      s_open_samples = 0;
#ifdef RT_USING_PM
      rt_pm_release(PM_SLEEP_MODE_IDLE);
#endif
#ifdef SF32_SOUND_LOG
      rt_kprintf("sound: path closed\n");
#endif
    }
  }
}

static bool start_thread() {
  if (s_started) return true;
  rt_event_init(&s_ev, "sndev", RT_IPC_FLAG_FIFO);
  rt_mb_init(&s_mb, "sndmb", s_mb_pool, 2, RT_IPC_FLAG_FIFO);
  rt_thread_t t = rt_thread_create("sound", sound_thread, RT_NULL, 2048, 12, 10);
  if (!t) return false;
  rt_thread_startup(t);
  s_started = true;
  return true;
}

bool SF32Sound::running() { return s_running; }

bool SF32Sound::ready() {
  // open and past the DAC's volume ramp after unmuting (short sounds play at full level)
  return s_running && s_open_samples >= SAMPLE_RATE * 750 / 1000;  // 350 ms still clipped a click
}

void SF32Sound::powerSave(bool on) {
  if (on == s_power_save) return;
  s_power_save = on;
  if (on && s_started) rt_mb_send(&s_mb, MSG_POWER_DOWN);  // wake the thread if it's idle (full: it's busy, and powers down when the path closes)
}

void SF32Sound::keepOpen(bool on, Volume vol) {
  s_keep_open = on && vol != VOL_OFF;
#ifdef SF32_SOUND_LOG
  rt_kprintf("sound: keep open %d (volume %d)\n", (int)s_keep_open, (int)vol);
#endif
  if (s_keep_open && !s_running && start_thread())
    rt_mb_send(&s_mb, (rt_ubase_t)MSG_OPEN | ((rt_ubase_t)(uint8_t)(int8_t)volume_half_db(vol) << 8));
}

void SF32Sound::tone(uint16_t hz, uint16_t ms, Volume vol, uint8_t count) {
  // like chime(): may replace a tap sound, nothing else (its notes are rewritten here)
  if (vol == VOL_OFF || (s_busy && !(s_cur_kind >= CHIME_CLICK && s_cur_kind <= CHIME_SWISH))) return;
  Note* n = CHIME_NOTES[CHIME_CUSTOM];
  n[0] = {hz, ms};
  n[1] = count > 1 ? Note{1, ms} : Note{0, 0};   // gap
  n[2] = count > 1 ? Note{hz, ms} : Note{0, 0};
  chime(CHIME_CUSTOM, vol);
}

void SF32Sound::chime(Chime kind, Volume vol) {
#ifdef SF32_SOUND_LOG
  if (s_busy) {
    struct rt_audio_device* ad = (struct rt_audio_device*)s_prc;
    rt_kprintf("sound: busy, kind %d skipped (running %d, step %d, queue %d/%d)\n", (int)kind, (int)s_running, s_step,
               ad && ad->replay ? (int)(rt_uint16_t)(ad->replay->queue.put_index - ad->replay->queue.get_index) : -1,
               ad && ad->replay ? (int)ad->replay->queue.size : -1);
  }
#endif
  if (vol == VOL_OFF) return;
  // busy: skipped, except that a chime (message, test, startup, beeps) replaces a tap sound
  // still playing (the thread takes a new sound at any time)
  bool tap_playing = s_cur_kind >= CHIME_CLICK && s_cur_kind <= CHIME_SWISH;
  bool is_tap = kind >= CHIME_CLICK && kind <= CHIME_SWISH;
  if (s_busy && (is_tap || !tap_playing)) return;
  if (!start_thread()) return;
  int volume = volume_half_db(vol);
  s_cur_kind = kind;
  s_busy = true;
  if (rt_mb_send(&s_mb, (rt_ubase_t)kind | ((rt_ubase_t)(uint8_t)(int8_t)volume << 8)) != RT_EOK) s_busy = false;
}
