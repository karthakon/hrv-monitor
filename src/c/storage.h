#pragma once
#include <pebble.h>

#define EPOCHS_PER_KEY 21
#define MAX_EPOCH_KEYS 24
#define MAX_NIGHTS 30

typedef struct __attribute__((packed)) {
  uint16_t mean_ppi;
  uint16_t rmssd;
  uint8_t beat_count;
  uint8_t stage;
  uint8_t quality_pct;
  uint8_t reserved;
} EpochRecord;

typedef struct __attribute__((packed)) {
  time_t date;
  uint16_t rmssd;
  uint16_t sdnn;
  uint16_t mean_ppi;
  uint16_t epoch_count;
  uint16_t mins_awake;
  uint16_t mins_light;
  uint16_t mins_deep;
  uint16_t mins_rem;
  uint32_t rejected;
  uint32_t rej_range;
  uint32_t rej_jump;
  time_t start_time;
  time_t end_time;
} NightSummary;

void storage_session_start(void);
void storage_epoch_write(const EpochRecord *rec);
uint16_t storage_epoch_count(void);
bool storage_epoch_read(uint16_t idx, EpochRecord *out);
void storage_night_save(const NightSummary *ns);
uint8_t storage_night_count(void);
bool storage_night_read(uint8_t idx_from_newest, NightSummary *out);

// --- SpO2 (separate keys so NightSummary layout is untouched) ---
#define SPO2_PER_KEY 60
#define MAX_SPO2_KEYS 2

typedef struct __attribute__((packed)) {
  uint16_t offset_min;   // minutes since session start
  uint8_t percent;
  uint8_t quality;       // HRMQuality cast to uint8 (255 = off-wrist)
} SpO2Sample;

typedef struct __attribute__((packed)) {
  uint8_t min_pct;
  uint8_t avg_pct;
  uint8_t count;
  uint8_t reserved;
} SpO2Night;

void storage_spo2_session_start(void);
void storage_spo2_write(const SpO2Sample *rec);
uint16_t storage_spo2_count(void);
bool storage_spo2_read(uint16_t idx, SpO2Sample *out);
// Save alongside the just-saved night (uses the current newest slot).
void storage_night_save_spo2(const SpO2Night *sn);
// False (and zeroed out) for nights recorded before SpO2 support.
bool storage_night_read_spo2(uint8_t idx_from_newest, SpO2Night *out);
