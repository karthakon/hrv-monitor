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
