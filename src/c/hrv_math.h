#pragma once
#include <pebble.h>

#define HRV_BUF_MAX 400
#define HRV_STALE_SEC 10

typedef struct {
  uint16_t ppi[HRV_BUF_MAX];
  uint16_t count;
  uint16_t last_accepted;
  uint32_t last_accepted_time;
  uint32_t rejected;
  uint32_t rej_quality;
  uint32_t rej_range;
  uint32_t rej_jump;
  uint32_t total_accepted;  // running total; count pins at HRV_BUF_MAX
} HrvBuffer;

void hrv_buf_reset(HrvBuffer *b);
bool hrv_buf_add(HrvBuffer *b, uint16_t ppi_ms, uint8_t quality, uint32_t now);
uint16_t hrv_rmssd(const HrvBuffer *b);
uint16_t hrv_sdnn(const HrvBuffer *b);
uint16_t hrv_mean_ppi(const HrvBuffer *b);
uint32_t hrv_ppi_variance(const HrvBuffer *b);
