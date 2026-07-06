#include "hrv_math.h"

void hrv_buf_reset(HrvBuffer *b) {
  b->count = 0;
  b->last_accepted = 0;
  b->rejected = 0;
}

bool hrv_buf_add(HrvBuffer *b, uint16_t ppi_ms, uint8_t quality) {
  if (quality == 0 || ppi_ms < 300 || ppi_ms > 2000) {
    b->rejected++;
    return false;
  }
  if (b->last_accepted > 0) {
    uint32_t diff = (ppi_ms > b->last_accepted) ?
      (ppi_ms - b->last_accepted) : (b->last_accepted - ppi_ms);
    if (diff * 5 > b->last_accepted) {
      b->rejected++;
      return false;
    }
  }
  if (b->count < HRV_BUF_MAX) {
    b->ppi[b->count++] = ppi_ms;
  } else {
    memmove(b->ppi, b->ppi + 1, (HRV_BUF_MAX - 1) * sizeof(uint16_t));
    b->ppi[HRV_BUF_MAX - 1] = ppi_ms;
  }
  b->last_accepted = ppi_ms;
  return true;
}

static uint32_t prv_isqrt(uint32_t n) {
  uint32_t x = n, y = (x + 1) / 2;
  if (n < 2) return n;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return x;
}

uint16_t hrv_mean_ppi(const HrvBuffer *b) {
  if (b->count == 0) return 0;
  uint32_t sum = 0;
  for (uint16_t i = 0; i < b->count; i++) sum += b->ppi[i];
  return (uint16_t)(sum / b->count);
}

uint16_t hrv_rmssd(const HrvBuffer *b) {
  if (b->count < 2) return 0;
  uint64_t sumsq = 0;
  for (uint16_t i = 1; i < b->count; i++) {
    int32_t d = (int32_t)b->ppi[i] - (int32_t)b->ppi[i - 1];
    sumsq += (uint64_t)(d * d);
  }
  return (uint16_t)prv_isqrt((uint32_t)(sumsq / (b->count - 1)));
}

uint32_t hrv_ppi_variance(const HrvBuffer *b) {
  if (b->count < 2) return 0;
  uint32_t mean = hrv_mean_ppi(b);
  uint64_t sumsq = 0;
  for (uint16_t i = 0; i < b->count; i++) {
    int32_t d = (int32_t)b->ppi[i] - (int32_t)mean;
    sumsq += (uint64_t)(d * d);
  }
  return (uint32_t)(sumsq / (b->count - 1));
}

uint16_t hrv_sdnn(const HrvBuffer *b) {
  return (uint16_t)prv_isqrt(hrv_ppi_variance(b));
}
