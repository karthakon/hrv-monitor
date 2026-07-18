#include <pebble.h>
#include "hrv_math.h"
#include "storage.h"
#include "sleep_stage.h"

#define NUM_SCREENS 4

static Window *s_window;
static Layer *s_canvas;
static uint8_t s_screen = 0;
static bool s_recording = false;

static uint32_t s_hr_events = 0;
static uint32_t s_hrv_events = 0;
static uint16_t s_last_ppi = 0;
static uint16_t s_last_hr = 0;

static HrvBuffer s_live_buf;
static HrvBuffer s_minute_buf;
static HrvBuffer s_night_buf;

static time_t s_session_start = 0;
static uint32_t s_night_baseline_var = 0;
static uint16_t s_mins[4] = {0, 0, 0, 0};
static uint8_t s_awake_streak = 0;
static SleepStage s_last_stage = StageLight;
#define AWAKE_DEBOUNCE 3

static void prv_close_minute(void) {
  if (!s_recording) return;
  EpochRecord rec;
  uint16_t total = s_minute_buf.count + (uint16_t)s_minute_buf.rejected;
  rec.mean_ppi = hrv_mean_ppi(&s_minute_buf);
  rec.rmssd = hrv_rmssd(&s_minute_buf);
  rec.beat_count = (s_minute_buf.count > 255) ? 255 : (uint8_t)s_minute_buf.count;
  rec.quality_pct = (total > 0) ? (uint8_t)((s_minute_buf.count * 100) / total) : 0;
  rec.reserved = 0;
  SleepStage st = sleep_stage_classify(&s_minute_buf, s_night_baseline_var);
  // Debounce: a raw Awake only sticks after AWAKE_DEBOUNCE consecutive Awake minutes;
  // otherwise hold the previous stage (kills flicker from the live activity mask).
  if (st == StageAwake) {
    s_awake_streak++;
    if (s_awake_streak < AWAKE_DEBOUNCE) st = s_last_stage;
  } else {
    s_awake_streak = 0;
  }
  s_last_stage = st;
  rec.stage = (uint8_t)st;
  s_mins[st]++;
  storage_epoch_write(&rec);
  if (s_night_buf.count >= 60 && s_night_baseline_var == 0) {
    s_night_baseline_var = hrv_ppi_variance(&s_night_buf);
  }
  hrv_buf_reset(&s_minute_buf);
}

static bool s_hrv_on = true;
static void prv_set_hrv(bool on) {
  if (on == s_hrv_on) return;
  s_hrv_on = on;
  health_service_set_hrv_sample_period(on ? 1 : 0);
}
static void prv_tick_handler(struct tm *tick_time, TimeUnits changed) {
  prv_close_minute();
  if (s_recording) {
    prv_set_hrv(true);  // continuous sampling (duty cycle disabled for staging validation)
  }
  if (!s_recording) layer_mark_dirty(s_canvas);
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate) {
    s_hr_events++;
    HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateRawBPM);
    if (hr > 0) s_last_hr = (uint16_t)hr;
  } else if ((int)event == 5) {
    s_hrv_events++;
    uint16_t ppi = (uint16_t)health_service_peek_hrv_ppi_ms();
    if (ppi > 0) {
      s_last_ppi = ppi;
      uint32_t now = (uint32_t)time(NULL);
      if (s_recording) {
        hrv_buf_add(&s_live_buf, ppi, 1, now);
        hrv_buf_add(&s_minute_buf, ppi, 1, now);
        hrv_buf_add(&s_night_buf, ppi, 1, now);
      } else {
        hrv_buf_add(&s_live_buf, ppi, 1, now);
      }
    }
  }
  if (!s_recording) layer_mark_dirty(s_canvas);
}

static void prv_start_recording(void) {
  s_recording = true;
  prv_set_hrv(true);
  s_session_start = time(NULL);
  s_night_baseline_var = 0;
  s_awake_streak = 0;
  s_last_stage = StageLight;
  for (int i = 0; i < 4; i++) s_mins[i] = 0;
  hrv_buf_reset(&s_minute_buf);
  hrv_buf_reset(&s_night_buf);
  storage_session_start();
}

static void prv_stop_recording(void) {
  prv_close_minute();
  s_recording = false;
  prv_set_hrv(true);
  NightSummary ns;
  ns.date = s_session_start;
  ns.rmssd = hrv_rmssd(&s_night_buf);
  ns.sdnn = hrv_sdnn(&s_night_buf);
  ns.mean_ppi = hrv_mean_ppi(&s_night_buf);
  ns.epoch_count = storage_epoch_count();
  // Query finalized Deep (accelerometer-based) from Pebble Health rather than
  // the lossy live peek. Live restful minutes were counted as Light above, so
  // move that many minutes from Light to Deep to keep total sleep consistent.
  time_t deep_end = time(NULL);
  int deep_sec = (int)health_service_sum(HealthMetricSleepRestfulSeconds,
                                         s_session_start, deep_end);
  uint16_t deep_min = (deep_sec > 0) ? (uint16_t)(deep_sec / 60) : 0;
  s_mins[StageDeep] = deep_min;
  if (deep_min <= s_mins[StageLight]) {
    s_mins[StageLight] -= deep_min;
  } else {
    s_mins[StageLight] = 0;
  }
  ns.mins_awake = s_mins[StageAwake];
  ns.mins_light = s_mins[StageLight];
  ns.mins_deep = s_mins[StageDeep];
  ns.mins_rem = s_mins[StageREM];
  ns.rejected = s_night_buf.rejected;
  ns.start_time = s_session_start;
  ns.end_time = time(NULL);
  if (ns.epoch_count >= 30) storage_night_save(&ns);
}

static void prv_baseline(uint16_t *avg7, uint16_t *base21_lo,
                         uint16_t *base21_hi, uint8_t *nights) {
  uint8_t n = storage_night_count();
  *nights = n;
  *avg7 = 0;
  *base21_lo = 0;
  *base21_hi = 0;
  if (n == 0) return;
  uint32_t sum7 = 0;
  uint8_t c7 = 0;
  uint32_t sum21 = 0, sumsq21 = 0;
  uint8_t c21 = 0;
  NightSummary ns;
  for (uint8_t i = 0; i < n && i < 21; i++) {
    if (!storage_night_read(i, &ns)) break;
    if (i < 7) { sum7 += ns.rmssd; c7++; }
    sum21 += ns.rmssd;
    sumsq21 += (uint32_t)ns.rmssd * ns.rmssd;
    c21++;
  }
  if (c7 > 0) *avg7 = (uint16_t)(sum7 / c7);
  if (c21 >= 2) {
    uint32_t mean = sum21 / c21;
    uint32_t var = (sumsq21 / c21) - (mean * mean);
    uint32_t sd = 0;
    while ((sd + 1) * (sd + 1) <= var) sd++;
    *base21_lo = (uint16_t)((mean > sd) ? (mean - sd) : 0);
    *base21_hi = (uint16_t)(mean + sd);
  }
}

static char s_txt[200];

static bool prv_last_night(NightSummary *out) {
  return storage_night_count() > 0 && storage_night_read(0, out);
}

static void prv_draw_live(GContext *ctx, GRect bounds) {
  snprintf(s_txt, sizeof(s_txt),
           "LIVE %s\n\nPPI: %u ms\nHR: %u bpm\n\nHR ev: %lu\nHRV ev: %lu\nRMSSD(5m): %u",
           s_recording ? "[REC]" : "[idle]",
           s_last_ppi, s_last_hr,
           (unsigned long)s_hr_events, (unsigned long)s_hrv_events,
           hrv_rmssd(&s_live_buf));
  graphics_draw_text(ctx, s_txt, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     bounds, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void prv_draw_session(GContext *ctx, GRect bounds) {
  time_t start = 0, end = 0;
  uint16_t beats, rmssd, sdnn, ppi;
  uint32_t rejected;
  if (s_recording) {
    start = s_session_start;
    end = time(NULL);
    beats = s_night_buf.count;
    rejected = s_night_buf.rejected;
    rmssd = hrv_rmssd(&s_night_buf);
    sdnn = hrv_sdnn(&s_night_buf);
    ppi = hrv_mean_ppi(&s_night_buf);
  } else {
    NightSummary ns;
    if (!prv_last_night(&ns)) {
      snprintf(s_txt, sizeof(s_txt), "SESSION\n\nNo data yet.");
      graphics_draw_text(ctx, s_txt, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                         bounds, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
      return;
    }
    start = ns.start_time;
    end = ns.end_time;
    beats = ns.epoch_count;
    rejected = ns.rejected;
    rmssd = ns.rmssd;
    sdnn = ns.sdnn;
    ppi = ns.mean_ppi;
  }
  uint32_t dur = (end > start) ? (uint32_t)(end - start) : 0;
  char t0[8] = "--:--", t1[8] = "--:--";
  if (start > 0) { struct tm *lt = localtime(&start); strftime(t0, sizeof(t0), "%H:%M", lt); }
  if (end > 0)   { struct tm *lt = localtime(&end);   strftime(t1, sizeof(t1), "%H:%M", lt); }
  snprintf(s_txt, sizeof(s_txt),
           "SESSION\n\n%s-%s\nDur: %luh %lum\nBeats: %u\nRej: %lu\nR/J: %lu/%lu\nRMSSD: %u\nSDNN: %u\nPPI: %u",
           t0, t1,
           (unsigned long)(dur / 3600), (unsigned long)((dur % 3600) / 60),
           beats, (unsigned long)rejected,
           (unsigned long)s_night_buf.rej_range, (unsigned long)s_night_buf.rej_jump,
           rmssd, sdnn, ppi);
  graphics_draw_text(ctx, s_txt, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     bounds, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void prv_draw_baseline(GContext *ctx, GRect bounds) {
  uint16_t avg7, lo, hi;
  uint8_t nights;
  prv_baseline(&avg7, &lo, &hi, &nights);
  const char *status;
  if (nights < 21) {
    status = "Collecting";
  } else if (avg7 < lo) {
    status = "LOW";
  } else if (avg7 > hi) {
    status = "HIGH";
  } else {
    status = "Balanced";
  }
  snprintf(s_txt, sizeof(s_txt),
           "BASELINE\n\nNights: %u/21\n7d RMSSD: %u\nBand: %u-%u\n\nStatus:\n%s",
           nights, avg7, lo, hi, status);
  graphics_draw_text(ctx, s_txt, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
                     bounds, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

static void prv_draw_timeline(GContext *ctx, GRect bounds) {
  uint16_t awake, light, deep, rem;
  if (s_recording) {
    awake = s_mins[StageAwake];
    light = s_mins[StageLight];
    deep = s_mins[StageDeep];
    rem = s_mins[StageREM];
  } else {
    NightSummary ns;
    if (!prv_last_night(&ns)) {
      snprintf(s_txt, sizeof(s_txt), "NIGHT\n\nNo data yet.");
      graphics_draw_text(ctx, s_txt, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                         bounds, GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
      return;
    }
    awake = ns.mins_awake;
    light = ns.mins_light;
    deep = ns.mins_deep;
    rem = ns.mins_rem;
  }
  uint16_t sleep = light + deep + rem;
  snprintf(s_txt, sizeof(s_txt),
           "NIGHT\n\nSleep: %uh %um\nAwake: %um\nLight: %uh %um\nDeep: %uh %um\nREM: %uh %um",
           sleep / 60, sleep % 60,
           awake,
           light / 60, light % 60,
           deep / 60, deep % 60,
           rem / 60, rem % 60);
  graphics_draw_text(ctx, s_txt, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(bounds.origin.x, bounds.origin.y, bounds.size.w, bounds.size.h - 44),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  uint16_t n = storage_epoch_count();
  if (n == 0) return;
  int16_t h = 36;
  int16_t y0 = bounds.size.h - h - 4;
  uint16_t max_px = bounds.size.w;
  EpochRecord rec;
  for (uint16_t x = 0; x < max_px; x++) {
    uint16_t idx = (uint16_t)(((uint32_t)x * n) / max_px);
    if (!storage_epoch_read(idx, &rec)) continue;
    int16_t bar = (rec.stage == 0) ? h : (rec.stage == 1) ? (h * 2 / 3)
                  : (rec.stage == 2) ? (h / 4) : (h / 2);
    GColor c = (rec.stage == 0) ? GColorRed : (rec.stage == 1) ? GColorVividCerulean
               : (rec.stage == 2) ? GColorOxfordBlue : GColorVividViolet;
    graphics_context_set_stroke_color(ctx, c);
    graphics_draw_line(ctx, GPoint(x, y0 + h), GPoint(x, y0 + h - bar));
  }
}

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  if (s_recording) {
    graphics_draw_text(ctx, "Sleeping\n\nHold Select\nto stop",
                       fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
                       bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }
  switch (s_screen) {
    case 0: prv_draw_live(ctx, bounds); break;
    case 1: prv_draw_session(ctx, bounds); break;
    case 2: prv_draw_baseline(ctx, bounds); break;
    case 3: prv_draw_timeline(ctx, bounds); break;
  }
}

static void prv_select_click(ClickRecognizerRef ref, void *ctx) {
  if (!s_recording && storage_epoch_count() == 0) {
    prv_start_recording();
  }
  layer_mark_dirty(s_canvas);
}
static void prv_select_long(ClickRecognizerRef ref, void *ctx) {
  if (s_recording) {
    prv_stop_recording();
  } else {
    prv_start_recording();
  }
  layer_mark_dirty(s_canvas);
}

static void prv_down_click(ClickRecognizerRef ref, void *ctx) {
  if (s_recording) return;
  s_screen = (s_screen + 1) % NUM_SCREENS;
  layer_mark_dirty(s_canvas);
}

static void prv_up_click(ClickRecognizerRef ref, void *ctx) {
  if (s_recording) return;
  s_screen = (s_screen + NUM_SCREENS - 1) % NUM_SCREENS;
  layer_mark_dirty(s_canvas);
}

static void prv_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 1500, prv_select_long, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_canvas = layer_create(GRect(4, 0, bounds.size.w - 8, bounds.size.h));
  layer_set_update_proc(s_canvas, prv_canvas_update);
  layer_add_child(root, s_canvas);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas);
}

#define PERSIST_VERSION_KEY 1
#define PERSIST_VERSION 3
static void prv_migrate(void) {
  int32_t v = persist_exists(PERSIST_VERSION_KEY) ? persist_read_int(PERSIST_VERSION_KEY) : 0;
  if (v < PERSIST_VERSION) {
    for (uint32_t k = 100; k <= 250; k++) if (persist_exists(k)) persist_delete(k);
    persist_write_int(PERSIST_VERSION_KEY, PERSIST_VERSION);
  }
}
static void prv_init(void) {
  prv_migrate();
  hrv_buf_reset(&s_live_buf);
  hrv_buf_reset(&s_minute_buf);
  hrv_buf_reset(&s_night_buf);
  s_window = window_create();
  window_set_click_config_provider(s_window, prv_click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);
  health_service_set_hrv_sample_period(1);
  health_service_events_subscribe(prv_health_handler, NULL);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
}

static void prv_deinit(void) {
  if (s_recording) prv_stop_recording();
  health_service_set_hrv_sample_period(0);
  health_service_events_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
  return 0;
}
