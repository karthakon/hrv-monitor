#pragma once
#include <pebble.h>

#define HRV_BUF_MAX 400

typedef struct {
  uint16_t ppi[HRV_BUF_MAX];
  uint16_t count;
  uint16_t last_accepted;
  uint32_t rejected;
  uint32_t rej_quality;
  uint32_t rej_range;
  uint32_t rej_jump;
} HrvBuffer;

void hrv_buf_reset(HrvBuffer *b);
bool hrv_buf_add(HrvBuffer *b, uint16_t ppi_ms, uint8_t quality);
uint16_t hrv_rmssd(const HrvBuffer *b);
uint16_t hrv_sdnn(const HrvBuffer *b);
uint16_t hrv_mean_ppi(const HrvBuffer *b);
uint32_t hrv_ppi_variance(const HrvBuffer *b);
