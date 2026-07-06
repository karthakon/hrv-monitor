#include "sleep_stage.h"

const char *sleep_stage_name(SleepStage s) {
  switch (s) {
    case StageAwake: return "Awake";
    case StageLight: return "Light";
    case StageDeep:  return "Deep";
    case StageREM:   return "REM";
    default:         return "?";
  }
}

SleepStage sleep_stage_classify(const HrvBuffer *minute_buf,
                                uint32_t night_baseline_variance) {
  HealthActivityMask act = health_service_peek_current_activities();
  if (act & HealthActivityRestfulSleep) return StageDeep;
  if (!(act & HealthActivitySleep)) return StageAwake;
  if (minute_buf->count >= 20 && night_baseline_variance > 0) {
    uint32_t v = hrv_ppi_variance(minute_buf);
    if (v * 2 > night_baseline_variance * 3) return StageREM;
  }
  return StageLight;
}
