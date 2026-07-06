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
  rec.stage = (uint8_t)st;
  s_mins[st]++;
  storage_epoch_write(&rec);
  if (s_night_buf.count >= 60 && s_night_baseline_var == 0) {
    s_night_baseline_var = hrv_ppi_variance(&s_night_buf);
  }
  hrv_buf_reset(&s_minute_buf);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits changed) {
  prv_close_minute();
  layer_mark_dirty(s_canvas);
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
      if (s_recording) {
        hrv_buf_add(&s_live_buf, ppi, 1);
        hrv_buf_add(&s_minute_buf, ppi, 1);
        hrv_buf_add(&s_night_buf, ppi, 1);
      } else {
        hrv_buf_add(&s_live_buf, ppi, 1);
      }
    }
  }
  layer_mark_dirty(s_canvas);
}

static void prv_start_recording(void) {
  s_recording = true;
  s_session_start = time(NULL);
  s_night_baseline_var = 0;
  for (int i = 0; i < 4; i++) s_mins[i] = 0;
  hrv_buf_reset(&s_minute_buf);
  hrv_buf_reset(&s_night_buf);
  storage_session_start();
}

static void prv_stop_recording(void) {
  prv_close_minute();
  s_recording = false;
  NightSummary ns;
  ns.date = s_session_start;
  ns.rmssd = hrv_rmssd(&s_night_buf);
  ns.sdnn = hrv_sdnn(&s_night_buf);
  ns.mean_ppi = hrv_mean_ppi(&s_night_buf);
  ns.epoch_count = storage_epoch_count();
  ns.mins_awake = s_mins[StageAwake];
  ns.mins_light = s_mins[StageLight];
  ns.mins_deep = s_mins[StageDeep];
  ns.mins_rem = s_mins[StageREM];
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

static char s_txt[160];

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
  uint32_t dur = 0;
  if (s_session_start > 0) dur = (uint32_t)(time(NULL) - s_session_start);
  snprintf(s_txt, sizeof(s_txt),
           "SESSION\n\nDur: %luh %lum\nBeats: %u\nRejected: %lu\nRMSSD: %u\nSDNN: %u\nMean PPI: %u",
           (unsigned long)(dur / 3600), (unsigned long)((dur % 3600) / 60),
           s_night_buf.count, (unsigned long)s_night_buf.rejected,
           hrv_rmssd(&s_night_buf), hrv_sdnn(&s_night_buf),
           hrv_mean_ppi(&s_night_buf));
  graphics_draw_text(ctx, s_txt, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
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
  snprintf(s_txt, sizeof(s_txt),
           "NIGHT\n\nAwake: %um\nLight: %uh %um\nDeep: %uh %um\nREM: %uh %um",
           s_mins[StageAwake],
           s_mins[StageLight] / 60, s_mins[StageLight] % 60,
           s_mins[StageDeep] / 60, s_mins[StageDeep] % 60,
           s_mins[StageREM] / 60, s_mins[StageREM] % 60);
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
    graphics_draw_line(ctx, GPoint(x, y0 + h), GPoint(x, y0 + h - bar));
  }
}

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  switch (s_screen) {
    case 0: prv_draw_live(ctx, bounds); break;
    case 1: prv_draw_session(ctx, bounds); break;
    case 2: prv_draw_baseline(ctx, bounds); break;
    case 3: prv_draw_timeline(ctx, bounds); break;
  }
}

static void prv_select_click(ClickRecognizerRef ref, void *ctx) {
  if (s_recording) {
    prv_stop_recording();
  } else {
    prv_start_recording();
  }
  layer_mark_dirty(s_canvas);
}

static void prv_down_click(ClickRecognizerRef ref, void *ctx) {
  s_screen = (s_screen + 1) % NUM_SCREENS;
  layer_mark_dirty(s_canvas);
}

static void prv_up_click(ClickRecognizerRef ref, void *ctx) {
  s_screen = (s_screen + NUM_SCREENS - 1) % NUM_SCREENS;
  layer_mark_dirty(s_canvas);
}

static void prv_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
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

static void prv_init(void) {
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
