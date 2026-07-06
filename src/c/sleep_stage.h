#pragma once
#include <pebble.h>
#include "hrv_math.h"

typedef enum {
  StageAwake = 0,
  StageLight = 1,
  StageDeep = 2,
  StageREM = 3
} SleepStage;

SleepStage sleep_stage_classify(const HrvBuffer *minute_buf,
                                uint32_t night_baseline_variance);
const char *sleep_stage_name(SleepStage s);
