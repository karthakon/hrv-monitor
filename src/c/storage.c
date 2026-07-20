#include "storage.h"

#define KEY_EPOCH_META 100
#define KEY_EPOCH_BASE 101
#define KEY_NIGHT_META 200
#define KEY_NIGHT_BASE 201
#define KEY_SPO2_META 300
#define KEY_SPO2_BASE 301
#define KEY_SPO2_NIGHT_BASE 400

typedef struct __attribute__((packed)) {
  uint16_t epoch_count;
} EpochMeta;

typedef struct __attribute__((packed)) {
  uint8_t night_count;
  uint8_t newest_slot;
} NightMeta;

static EpochRecord s_key_buf[EPOCHS_PER_KEY];

void storage_session_start(void) {
  EpochMeta m = { .epoch_count = 0 };
  persist_write_data(KEY_EPOCH_META, &m, sizeof(m));
}

uint16_t storage_epoch_count(void) {
  EpochMeta m;
  if (persist_read_data(KEY_EPOCH_META, &m, sizeof(m)) != sizeof(m)) return 0;
  return m.epoch_count;
}

void storage_epoch_write(const EpochRecord *rec) {
  EpochMeta m = { .epoch_count = storage_epoch_count() };
  uint16_t max_epochs = EPOCHS_PER_KEY * MAX_EPOCH_KEYS;
  if (m.epoch_count >= max_epochs) return;
  uint16_t key = KEY_EPOCH_BASE + (m.epoch_count / EPOCHS_PER_KEY);
  uint16_t slot = m.epoch_count % EPOCHS_PER_KEY;
  memset(s_key_buf, 0, sizeof(s_key_buf));
  persist_read_data(key, s_key_buf, sizeof(s_key_buf));
  s_key_buf[slot] = *rec;
  persist_write_data(key, s_key_buf, sizeof(s_key_buf));
  m.epoch_count++;
  persist_write_data(KEY_EPOCH_META, &m, sizeof(m));
}

bool storage_epoch_read(uint16_t idx, EpochRecord *out) {
  if (idx >= storage_epoch_count()) return false;
  uint16_t key = KEY_EPOCH_BASE + (idx / EPOCHS_PER_KEY);
  uint16_t slot = idx % EPOCHS_PER_KEY;
  if (persist_read_data(key, s_key_buf, sizeof(s_key_buf)) != (int)sizeof(s_key_buf)) return false;
  *out = s_key_buf[slot];
  return true;
}

static void prv_night_meta(NightMeta *m) {
  if (persist_read_data(KEY_NIGHT_META, m, sizeof(*m)) != (int)sizeof(*m)) {
    m->night_count = 0;
    m->newest_slot = 0;
  }
}

void storage_night_save(const NightSummary *ns) {
  NightMeta m;
  prv_night_meta(&m);
  uint8_t slot;
  if (m.night_count == 0) {
    slot = 0;
  } else {
    slot = (m.newest_slot + 1) % MAX_NIGHTS;
  }
  persist_write_data(KEY_NIGHT_BASE + slot, ns, sizeof(*ns));
  m.newest_slot = slot;
  if (m.night_count < MAX_NIGHTS) m.night_count++;
  persist_write_data(KEY_NIGHT_META, &m, sizeof(m));
}

uint8_t storage_night_count(void) {
  NightMeta m;
  prv_night_meta(&m);
  return m.night_count;
}

void storage_spo2_session_start(void) {
  uint16_t c = 0;
  persist_write_data(KEY_SPO2_META, &c, sizeof(c));
}

uint16_t storage_spo2_count(void) {
  uint16_t c;
  if (persist_read_data(KEY_SPO2_META, &c, sizeof(c)) != sizeof(c)) return 0;
  return c;
}

static SpO2Sample s_spo2_key_buf[SPO2_PER_KEY];

void storage_spo2_write(const SpO2Sample *rec) {
  uint16_t c = storage_spo2_count();
  if (c >= SPO2_PER_KEY * MAX_SPO2_KEYS) return;
  uint16_t key = KEY_SPO2_BASE + (c / SPO2_PER_KEY);
  uint16_t slot = c % SPO2_PER_KEY;
  memset(s_spo2_key_buf, 0, sizeof(s_spo2_key_buf));
  persist_read_data(key, s_spo2_key_buf, sizeof(s_spo2_key_buf));
  s_spo2_key_buf[slot] = *rec;
  persist_write_data(key, s_spo2_key_buf, sizeof(s_spo2_key_buf));
  c++;
  persist_write_data(KEY_SPO2_META, &c, sizeof(c));
}

bool storage_spo2_read(uint16_t idx, SpO2Sample *out) {
  if (idx >= storage_spo2_count()) return false;
  uint16_t key = KEY_SPO2_BASE + (idx / SPO2_PER_KEY);
  uint16_t slot = idx % SPO2_PER_KEY;
  if (persist_read_data(key, s_spo2_key_buf, sizeof(s_spo2_key_buf)) !=
      (int)sizeof(s_spo2_key_buf)) return false;
  *out = s_spo2_key_buf[slot];
  return true;
}

void storage_night_save_spo2(const SpO2Night *sn) {
  NightMeta m;
  prv_night_meta(&m);
  if (m.night_count == 0) return;
  persist_write_data(KEY_SPO2_NIGHT_BASE + m.newest_slot, sn, sizeof(*sn));
}

bool storage_night_read_spo2(uint8_t idx_from_newest, SpO2Night *out) {
  memset(out, 0, sizeof(*out));
  NightMeta m;
  prv_night_meta(&m);
  if (idx_from_newest >= m.night_count) return false;
  int slot = ((int)m.newest_slot - (int)idx_from_newest + MAX_NIGHTS) % MAX_NIGHTS;
  return persist_read_data(KEY_SPO2_NIGHT_BASE + slot, out, sizeof(*out)) == (int)sizeof(*out);
}

bool storage_night_read(uint8_t idx_from_newest, NightSummary *out) {
  NightMeta m;
  prv_night_meta(&m);
  if (idx_from_newest >= m.night_count) return false;
  int slot = ((int)m.newest_slot - (int)idx_from_newest + MAX_NIGHTS) % MAX_NIGHTS;
  int r = persist_read_data(KEY_NIGHT_BASE + slot, out, sizeof(*out));
  return r == (int)sizeof(*out);
}
