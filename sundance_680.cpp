// Sundance 680-series (Peyton) additions: single-button light cycle, heater mode, spa date,
// and HA -> spa clock/date sync by driving the topside menu. Mapped from live bus captures
// against the 680 owner's manual (2026-10-22).
#include "sundance_spa.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace esphome {
namespace sundance_spa {

static const char *const TAG = "sundance_spa.680";

// ---------------------------------------------------------------- lights
// Light button order from the manual, index 1..10 (0 = off).
static const char *const LIGHT680_NAMES[] = {"Off",   "High Speed Blend", "Low Speed Blend", "Frozen Blend",
                                             "Blue",  "Violet",           "Red",             "Amber",
                                             "Green", "Aqua",             "Near White"};
static const int LIGHT680_COUNT = 10;
static const uint32_t LIGHT_ADVANCE_WINDOW_MS = 3000;  // a press within this advances the colour
static const uint32_t LIGHT_OFF_GAP_MS = 6000;         // a press after this gap turns the light off
static const uint32_t LIGHT_CONFIRM_TIMEOUT_MS = 2500;
static const int LIGHT_MAX_PRESSES = 30;

const char *SundanceSpa::light680_name(int index) {
  if (index < 0 || index > LIGHT680_COUNT)
    return "Unknown";
  return LIGHT680_NAMES[index];
}

int SundanceSpa::light680_index_from_name_(const std::string &name) {
  for (int i = 0; i <= LIGHT680_COUNT; i++) {
    if (name == LIGHT680_NAMES[i])
      return i;
  }
  return NO_CHANGE_REQUESTED;
}

// Lights frame: L1 = 100 on / 0 off, L4 = mode, L9 = blend speed (2 fast, 4 slow).
int SundanceSpa::light680_index_() const {
  if (this->light_brightness_ < 0)
    return NO_CHANGE_REQUESTED;
  if (this->light_brightness_ == 0)
    return 0;
  switch (this->light_mode_) {
    case 128:
      return this->light_speed_ == 4 ? 2 : 1;
    case 255:
      return 3;
    case 2:
      return 4;
    case 7:
      return 5;
    case 6:
      return 6;
    case 8:
      return 7;
    case 3:
      return 8;
    case 9:
      return 9;
    case 1:
      return 10;
    default:
      return NO_CHANGE_REQUESTED;
  }
}

void SundanceSpa::set_light_680(const std::string &name) {
  const int want = light680_index_from_name_(name);
  if (want == NO_CHANGE_REQUESTED) {
    ESP_LOGW(TAG, "Unknown light option '%s'", name.c_str());
    return;
  }
  if (const char *lk = this->lock_blocking_(LOCKUSE_LIGHT)) {
    this->decoder_notice_(std::string("Light not changed: ") + lk + " is on");
    this->publish_light_selects_();
    this->publish_light_status_();
    return;
  }
  if (this->light680_index_() == want && this->wanted_light680_ == NO_CHANGE_REQUESTED) {
    ESP_LOGD(TAG, "Light already %s", name.c_str());
    return;
  }
  this->wanted_light680_ = want;
  this->light680_expect_ = NO_CHANGE_REQUESTED;
  this->light680_presses_ = 0;
  ESP_LOGI(TAG, "Light -> %s (current %s)", name.c_str(), light680_name(this->light680_index_()));
  this->publish_light_selects_();
  this->publish_light_status_();
  this->service_light680_();
}

void SundanceSpa::service_light680_() {
  if (this->wanted_light680_ == NO_CHANGE_REQUESTED || this->channel_ < 0 || !this->tx_queue_.empty() ||
      this->menu_in_use_())
    return;
  const uint32_t now = millis();
  const int cur = this->light680_index_();
  if (cur == NO_CHANGE_REQUESTED)
    return;

  if (cur == this->wanted_light680_) {
    ESP_LOGI(TAG, "Light reached %s (%d presses)", light680_name(cur), this->light680_presses_);
    this->wanted_light680_ = NO_CHANGE_REQUESTED;
    this->light680_expect_ = NO_CHANGE_REQUESTED;
    this->publish_light_selects_();
    this->publish_light_status_();
    return;
  }
  if (this->light680_expect_ != NO_CHANGE_REQUESTED) {
    if (cur != this->light680_expect_ && now - this->light680_sent_ms_ < LIGHT_CONFIRM_TIMEOUT_MS)
      return;  // wait for the spa to report the step we pressed for
    this->light680_expect_ = NO_CHANGE_REQUESTED;
  }
  if (this->light680_presses_ >= LIGHT_MAX_PRESSES) {
    this->decoder_notice_("Light: gave up after " + std::to_string(this->light680_presses_) + " presses (now " +
                          light680_name(cur) + ")");
    this->wanted_light680_ = NO_CHANGE_REQUESTED;
    this->publish_light_selects_();
    this->publish_light_status_();
    return;
  }
  if (now - this->last_panel_press_ms_ < 1500)
    return;  // someone is at the topside; let them finish

  const uint32_t since = now - this->last_light_press_ms_;
  int expect;
  if (this->wanted_light680_ == 0) {
    if (since < LIGHT_OFF_GAP_MS)
      return;  // a press now would advance the colour instead of switching off
    expect = 0;
  } else if (cur == 0) {
    expect = 1;
  } else if (since <= LIGHT_ADVANCE_WINDOW_MS) {
    expect = cur % LIGHT680_COUNT + 1;
  } else if (since >= LIGHT_OFF_GAP_MS) {
    expect = 0;  // the next press switches off; the following one restarts at High Speed Blend
  } else {
    return;  // ambiguous window (3-6 s after the last press): wait it out
  }
  ESP_LOGD(TAG, "Light step %s -> press (expect %s, want %s)", light680_name(cur), light680_name(expect),
           light680_name(this->wanted_light680_));
  this->queue_press_(BTN_LIGHT_COLOR);
  this->last_light_press_ms_ = now;
  this->light680_sent_ms_ = now;
  this->light680_expect_ = expect;
  this->light680_presses_++;
}

// ---------------------------------------------------------------- status-frame fields
static const char *heater_mode_name_(int raw) {
  switch (raw) {
    case 32:
      return "Auto";
    case 34:
      return "Eco";
    case 36:
      return "Day";
    default:
      return nullptr;
  }
}

// f7 = (day << 3) XOR month. Verified on the spa on 2026-11-07 by setting the date to 8 Jul 2026 (= 71);
// fits every earlier sample too: reset JAN 01 = 9, 22 Oct = 186, 23 Feb = 186, 23 Oct = 178. The old
// ((day ^ 1) << 3) + (month ^ 8) matched those but gave the wrong day for even days Jan-Jul.
// A day (1-31) never overflows the byte. Still ambiguous (22 Oct == 23 Feb: months m and m+8 differ
// only in day bit 0), so pick the candidate nearest HA's date.
static int encode_spa_date_(int day, int month) { return ((day << 3) ^ month) & 0xFF; }

bool SundanceSpa::decode_spa_date_(int &year, int &month, int &day) const {
  if (this->prior_status_.size() < 16)
    return false;
  const int f7 = this->prior_status_[7];
  year = 2000 + (this->prior_status_[9] ^ 1);
  int best_m = 0, best_d = 0, best_dist = 100000;
  int ref = -1;
  if (this->time_ != nullptr) {
    auto now = this->time_->now();
    if (now.is_valid())
      ref = (now.month - 1) * 31 + now.day_of_month;
  }
  // f7 = (day << 3) XOR month: the low three bits are the month's, the rest the day (bit 0 of the day
  // is flipped for months 9-12), so each month gives at most one day.
  for (int m = 1; m <= 12; m++) {
    const int rest = f7 ^ m;
    if ((rest & 7) != 0)
      continue;
    const int d = rest >> 3;
    if (d < 1 || d > 31 || encode_spa_date_(d, m) != f7)
      continue;
    int dist = 0;
    if (ref >= 0) {
      const int v = (m - 1) * 31 + d;
      dist = std::abs(v - ref);
      dist = std::min(dist, 372 - dist);
    }
    if (dist < best_dist) {
      best_dist = dist;
      best_m = m;
      best_d = d;
    }
  }
  if (best_m == 0)
    return false;
  month = best_m;
  day = best_d;
  return true;
}

// The topside screen code (f13) arrives XOR-masked by a key = (ClearRay days & 0xFF) ^ 78, so the main
// screen (78) reads as the low byte of the ClearRay counter (verified 2026-10-31 by setting the counter:
// 325 -> 11, 320 -> 14, 250 -> 180, Off -> 78; and 22-25 Oct the main screen read 78..75 at 334..331).
// It changes at midnight when the counter counts down and when the counter is set in the UV menu (followed
// in observe_menu_values_). Learn it whenever nobody has pressed anything for a minute: the panel is then
// back on the main screen (menus time out after 5-25 s).
// f2 bit5: set while the panel is editing a value in a menu (verified 2026-10-22/23).
bool SundanceSpa::menu_editing_() const {
  return this->prior_status_.size() >= 16 && (this->prior_status_[2] & 0x20) != 0;
}

int SundanceSpa::screen_() const {
  return this->prior_status_.size() < 16 ? -1 : (this->prior_status_[13] ^ this->screen_key_);
}

void SundanceSpa::learn_screen_key_() {
  const uint32_t now = millis();
  const int raw = this->prior_status_[13];
  if (raw != this->f13_steady_raw_) {
    this->f13_steady_raw_ = raw;
    this->f13_steady_ms_ = now;
  }
  const uint32_t last = std::max(this->last_panel_press_ms_, this->last_own_press_ms_);
  if (now < 60000 && last == 0)
    return;  // just booted: wait a minute of quiet first
  if (now - last < 60000)
    return;
  // Any key 0..255 is possible, so a quiet screen is only taken for the main one when it has been steady
  // for 20 s (the main screen is static) and its low byte fits the ClearRay counter we know: the same day,
  // or one less (the midnight countdown, published from here without opening the menu).
  const int k = raw ^ SCREEN_MAIN;
  const bool steady = now - this->f13_steady_ms_ >= 20000;
  const int d = this->uv_days_;
  if (steady && k == this->screen_key_) {
    this->bad_main_screen_ms_ = 0;
    return;
  }
  if (steady && (d < 0 || raw == (d & 0xFF) || (d > 0 && raw == ((d - 1) & 0xFF)))) {
    ESP_LOGI(TAG, "Screen code key %d -> %d (main screen reads %d)", this->screen_key_, k, raw);
    this->screen_key_ = k;
    this->bad_main_screen_ms_ = 0;
    if (d > 0 && raw == ((d - 1) & 0xFF))
      this->publish_uv_days_(d - 1);
    return;
  }
  // Quiet but not the main screen we expect. After 10 min: a steady code means the counter was set while
  // we did not see it (take the key, re-read the counter); a changing one is a screen we don't know (an
  // error message, for example).
  if (this->bad_main_screen_ms_ == 0) {
    this->bad_main_screen_ms_ = now;
    return;
  }
  if (now - this->bad_main_screen_ms_ < 600000)
    return;
  this->bad_main_screen_ms_ = now;  // at most every 10 min while it lasts
  if (steady) {
    ESP_LOGW(TAG, "Screen code key %d -> %d (ClearRay counter changed unseen)", this->screen_key_, k);
    this->screen_key_ = k;
    this->uv_days_ = -1;
    this->decoder_notice_("Screen key jumped to " + std::to_string(k) + " (ClearRay counter changed unseen); re-reading");
    this->request_settings_read(false);
  } else {
    this->decoder_notice_("Unknown topside screen code " + std::to_string(raw));
  }
}

// When the spa's minute changes, its clock is exactly at hh:mm:00, so HA's time at that moment gives
// the spa offset in seconds (positive = spa ahead). Recomputed at every rollover; cleared by a sync.
void SundanceSpa::track_spa_offset_() {
  // While the topside shows a menu the reported time does not advance; back on the main screen it jumps
  // to the current minute, which would look like a rollover at the wrong second (owner's test 27 Oct:
  // time menu open until 06:58:21, the real rollovers stayed at :09). Only measure once the main screen
  // has been up for 2 s, so that jump is never taken for a rollover.
  const int spa_tod_min = this->spa_hour_ * 60 + this->spa_minute_;
  const uint32_t now_ms = millis();
  if (this->screen_() != SCREEN_MAIN) {
    this->main_screen_since_ms_ = 0;
    this->spa_tod_prev_min_ = -1;
    return;
  }
  if (this->main_screen_since_ms_ == 0)
    this->main_screen_since_ms_ = now_ms;
  if (now_ms - this->main_screen_since_ms_ < 2000) {
    this->spa_tod_prev_min_ = spa_tod_min;
    return;
  }
  if (this->spa_tod_prev_min_ >= 0 && spa_tod_min == (this->spa_tod_prev_min_ + 1) % 1440 &&
      this->time_ != nullptr && !this->clock_sync_active_()) {
    auto t = this->time_->now();
    if (t.is_valid()) {
      int diff = spa_tod_min * 60 - (t.hour * 3600 + t.minute * 60 + t.second);
      if (diff > 43200)
        diff -= 86400;
      if (diff < -43200)
        diff += 86400;
      if (!this->spa_offset_valid_ || std::abs(diff - this->spa_offset_s_) > 2)
        ESP_LOGI(TAG, "Spa clock offset %+d s", diff);
      this->spa_offset_s_ = diff;
      this->spa_offset_valid_ = true;
      if (this->sync_eval_pending_) {
        // A sync that did not bring the offset under a minute counts as failed; after two in a row only
        // a larger error (>= 120 s) triggers another one, so a sync can never repeat every 30 min.
        this->sync_eval_pending_ = false;
        this->sync_fail_streak_ = std::abs(diff) >= 60 ? this->sync_fail_streak_ + 1 : 0;
        if (this->sync_fail_streak_ > 0) {
          char buf[64];
          snprintf(buf, sizeof(buf), "Clock sync left the spa %+d s off (%d in a row)", diff, this->sync_fail_streak_);
          this->decoder_notice_(buf);
        }
      }
    }
  }
  this->spa_tod_prev_min_ = spa_tod_min;
}

void SundanceSpa::update_680_status_() {
  const auto &d = this->prior_status_;
  if (d.size() < 16)
    return;
  this->track_spa_offset_();
  this->learn_screen_key_();
  for (const auto &raw : this->raw_status_sensors_) {
    const float v = d[raw.first];
    if (!raw.second->has_state() || raw.second->state != v)
      raw.second->publish_state(v);
  }
  if (this->screen_key_sensor_ != nullptr &&
      (!this->screen_key_sensor_->has_state() || this->screen_key_sensor_->state != this->screen_key_))
    this->screen_key_sensor_->publish_state(this->screen_key_);
  this->service_lock_event_();
  if (this->lock_text_sensor_ != nullptr) {
    // Access Lock = f1 bit3, Temperature Lock = f10 bit4 (verified 2026-10-23). Settings / Service
    // Lock are tracked from panel events (see service_lock_event_()); all four verified 2026-10-26.
    const char *name = (d[1] & 0x08)    ? "Access Lock"
                       : (d[10] & 0x10) ? "Temperature Lock"
                       : this->sl_on_   ? "Settings Lock"
                       : this->svl_on_  ? "Service Lock"
                                        : "Off";
    if (!this->lock_text_sensor_->has_state() || this->lock_text_sensor_->state != name)
      this->lock_text_sensor_->publish_state(name);
  }
  if (this->heater_mode_text_sensor_ != nullptr) {
    const char *name = heater_mode_name_(d[6]);
    char buf[16];
    if (name == nullptr) {
      snprintf(buf, sizeof(buf), "Unknown (%u)", d[6]);
      name = buf;
    }
    if (!this->heater_mode_text_sensor_->has_state() || this->heater_mode_text_sensor_->state != name)
      this->heater_mode_text_sensor_->publish_state(name);
  }
  if (this->date_text_sensor_ != nullptr && this->screen_() == SCREEN_MAIN) {
    int y, m, dd;
    if (this->decode_spa_date_(y, m, dd)) {
      char buf[16];
      // DD/MM/YYYY like every other date the owner sees (HA did not reformat the ISO date with
      // device_class date, 2026-10-23); no device_class in the YAML.
      snprintf(buf, sizeof(buf), "%02d/%02d/%04d", dd, m, y);
      if (!this->date_text_sensor_->has_state() || this->date_text_sensor_->state != buf)
        this->date_text_sensor_->publish_state(buf);
    }
  }
  this->observe_menu_values_();
  this->service_clock_sync_();
}

// ---------------------------------------------------------------- menu values (passive)
// The spa only exposes PF settings and the ClearRay counter while their menu screen is open.
// Watch every screen transition (ours or someone at the topside) and publish what was shown
// or saved:  PF 73 -> 70/71 start hour -Jets1-> 113 cycles -Jets1-> 68 duration -Jets1-> 73
//            UV 90 -> 91 days shown -> 88 days being chosen -Jets1-> 90 (under the new counter's key)
// ClearRay counter on screens 91/88: f15 = (days & 0xFF) ^ (days >> 8), 128 = Off (verified 2026-10-31:
// 325 -> 68, 320 -> 65, 250 -> 250, Off -> 128; earlier 334 -> 79, 333 -> 76). The low byte is also in
// the screen key (key ^ 78), which tells the ranges apart. -1 if f15 does not fit the key (a frame where
// the value byte lags the screen code).
static int uv_days_from_(int f15, int key) {
  const int low = key ^ SCREEN_MAIN;
  if (f15 == 128 && low == 0)
    return UV_OFF;
  const int hi = f15 ^ low;
  return hi == 0 || hi == 1 ? hi * 256 + low : -1;
}

static void publish_text_(text_sensor::TextSensor *s, const char *v) {
  if (s != nullptr && (!s->has_state() || s->state != v))
    s->publish_state(v);
}

void SundanceSpa::publish_uv_days_(int days) {
  if (days == UV_OFF) {
    this->uv_days_ = 0;
    publish_text_(this->uv_change_sensor_, "Off");
    return;
  }
  if (days < 0 || days > 400)
    return;
  this->uv_days_ = days;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", days);
  publish_text_(this->uv_change_sensor_, days == 0 ? "Change Now" : buf);
}

// Water Refresh: f12 = days; 0 with f2 bit7 = Off (verified 2026-10-25: bit7 set only while H2O is
// Off; FC Off does not set it), 0 without it = due. f2 bit7 is also set after a power cut until the
// clock is set, so an expired counter right after a power cut would read Off until then.
// Filter Change: f3 ^ 0xFA = days; Off and 0 days both send 0xFA (see fc_zero_kind_).
void SundanceSpa::publish_counters_() {
  const auto &d = this->prior_status_;
  if (d.size() < 16)
    return;
  char buf[8];
  if (d[12] != 0) {
    snprintf(buf, sizeof(buf), "%d", d[12]);
    publish_text_(this->h2o_change_sensor_, buf);
  } else {
    publish_text_(this->h2o_change_sensor_, (d[2] & 0x80) ? "Off" : "Change Now");
  }
  if (d[3] != 0xFA) {
    const int days = static_cast<uint8_t>(d[3] ^ 0xFA);
    this->fc_last_days_ = days;
    this->fc_zero_kind_ = 0;
    snprintf(buf, sizeof(buf), "%d", days);
    publish_text_(this->filter_change_sensor_, buf);
    return;
  }
  if (this->fc_zero_kind_ == 0 && this->fc_last_days_ == 1)
    this->fc_zero_kind_ = 2;  // counted down to 0: filter due
  if (this->fc_zero_kind_ == 1)
    publish_text_(this->filter_change_sensor_, "Off");
  else if (this->fc_zero_kind_ == 2)
    publish_text_(this->filter_change_sensor_, "Change Now");
  // else unknown until the FC menu is seen (menu confirm, boot or nightly read)
}

// ---------------------------------------------------------------- Settings / Service Lock
void SundanceSpa::on_panel_press_(uint8_t btn) {
  if (btn == BTN_CLEAR_RAY)
    this->last_circ_press_ms_ = millis();
  if (this->prior_status_.size() < 16)
    return;
  const int screen = this->screen_();
  int ev = 0;
  if (btn == BTN_P1 && screen == SCREEN_LOCK_SL)
    ev = 1;
  else if (btn == BTN_P1 && screen == SCREEN_LOCK_SVL)
    ev = 2;
  else if (btn == BTN_MENU_HOLD)
    ev = 3;
  if (ev == 0)
    return;
  this->lock_event_ = ev;
  this->lock_event_ms_ = millis();
  this->lock_event_f14_ = this->prior_status_[14];
}

// f14 bits 0-5 = a slowly drifting temperature-like value XOR flags: bit1 Settings Lock, bit2 Service
// Lock, bit5 pump running (bit6 = °F). Verified 2026-10-25: SL confirm 31 -> 29, unlock 29 -> 31;
// SVL confirm 31 -> 27, unlock 27 -> 63 (with the purge pump). A lock change is accepted only when
// the matching bit flips (bits 0 and 5 ignored) within 5 s of the panel event that causes it.
void SundanceSpa::service_lock_event_() {
  // The controller releases a Service Lock by itself after 2 hours (manual 10.10) without any panel event, so the
  // tracked flag is cleared 2 h 15 s after it was first seen on (or after boot). Measured 8 Nov 2026: engaged
  // 12:14:08, the spa released at 14:14:07.6 (f14 27 -> 62, purge until 31 at 14:15:18).
  if (this->svl_on_) {
    if (this->svl_since_ms_ == 0) {
      this->svl_since_ms_ = millis() | 1;
    } else if (millis() - this->svl_since_ms_ > 2UL * 3600UL * 1000UL + 15000UL) {
      this->svl_on_ = false;
      this->svl_since_ms_ = 0;
      const uint8_t locks = this->sl_on_ ? 1 : 0;
      this->lock_pref_.save(&locks);
      this->decoder_notice_("Service Lock timed out after 2 hours: shown as released");
    }
  } else {
    this->svl_since_ms_ = 0;
  }
  if (this->lock_event_ == 0)
    return;
  const uint8_t diff = (this->prior_status_[14] ^ this->lock_event_f14_) & 0x1E;
  bool changed = false;
  if (diff == 0x02 && (this->lock_event_ == 1 || this->lock_event_ == 3)) {
    this->sl_on_ = this->lock_event_ == 1;
    changed = true;
  } else if (diff == 0x04 && (this->lock_event_ == 2 || this->lock_event_ == 3)) {
    this->svl_on_ = this->lock_event_ == 2;
    changed = true;
  }
  if (changed) {
    ESP_LOGI(TAG, "Lock: Settings Lock %s, Service Lock %s", this->sl_on_ ? "on" : "off",
             this->svl_on_ ? "on" : "off");
    const uint8_t locks = (this->sl_on_ ? 1 : 0) | (this->svl_on_ ? 2 : 0);
    this->lock_pref_.save(&locks);
    this->lock_event_ = 0;
  } else if (diff != 0 || millis() - this->lock_event_ms_ > 5000) {
    this->lock_event_ = 0;  // no matching flip: not a lock change
  }
}

// LANG edit screen f15 for languages 0 English, 1 French, 2 Spanish, 3 German (Warmer order).
// Observed: key 0 (2026-10-23) and key 115 (2026-11-06) plain; key 14 (2026-11-03) shifted by 2
// (1 German, 2 English). PROVISIONAL model: shifted by 2 when key bit 2 is set (14 yes; 0, 112, 115 no) -
// only three keys seen. The LANG display screen (first Warmer) always shows f15 0 and carries no value.
static int lang_code_(int lang, int key) { return (key & 4) ? (lang ^ 2) & 3 : lang & 3; }

// from_screen: value is an f15 code (decoded here) and is kept for the next boot; otherwise it is the
// language restored at boot.
void SundanceSpa::publish_language_(int value, bool from_screen) {
  static const char *const NAMES[] = {"English", "French", "Spanish", "German"};
  if (from_screen && value >= 0 && value < 4) {
    value = lang_code_(value, this->screen_key_);  // XOR is its own inverse
    uint8_t saved = 255;
    if (!this->lang_pref_.load(&saved) || saved != value) {
      saved = static_cast<uint8_t>(value);
      this->lang_pref_.save(&saved);
    }
  }
  if (this->language_text_sensor_ == nullptr)
    return;
  char buf[16];
  const char *name = buf;
  if (value >= 0 && value < 4)
    name = NAMES[value];
  else
    snprintf(buf, sizeof(buf), "Unknown (%d)", value);
  if (!this->language_text_sensor_->has_state() || this->language_text_sensor_->state != name)
    this->language_text_sensor_->publish_state(name);
}

void SundanceSpa::observe_menu_values_() {
  const int prev = this->menu_prev_screen_;
  const int prev_value = this->menu_prev_value_;
  // Jets 1 on a UV days screen saves the counter, and from that frame on the screen codes use the new
  // counter's key: back on the UV screen under a key whose low byte fits the value just shown.
  if ((prev == SCREEN_UV_DAYS || prev == SCREEN_UV_EDIT) && this->prior_status_.size() >= 16) {
    const int now_screen = this->screen_();
    if (now_screen != prev && now_screen != SCREEN_UV_DAYS && now_screen != SCREEN_UV_EDIT &&
        now_screen != SCREEN_UV && now_screen != SCREEN_MAIN) {
      const int k = this->prior_status_[13] ^ SCREEN_UV;
      const int days = uv_days_from_(prev_value, k);
      if (days != -1) {
        ESP_LOGI(TAG, "ClearRay Bulb set to %d days (screen code key %d -> %d)", days, this->screen_key_, k);
        this->screen_key_ = k;
      }
    }
  }
  const int screen = this->screen_();
  const int value = this->prior_status_[15];
  if (screen != prev) {
    // UV counter: value shown on entering the display screen, final value on confirm.
    if (screen == SCREEN_UV_DAYS && prev != SCREEN_UV_DAYS)
      this->uv_display_pending_ = true;
    if ((prev == SCREEN_UV_DAYS || prev == SCREEN_UV_EDIT) && screen == SCREEN_UV) {
      const int days = uv_days_from_(prev_value, this->screen_key_);
      if (days != -1)
        this->publish_uv_days_(days);
    }
    // PF settings: published on each Jets 1 confirm (unconfirmed edits time out and are discarded).
    if ((prev == SCREEN_PF_START || prev == SCREEN_PF_START_EDIT) && screen == SCREEN_PF_CYCLES &&
        this->pf_start_text_sensor_ != nullptr) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:00", prev_value);  // f15 = start hour, 24 h
      if (!this->pf_start_text_sensor_->has_state() || this->pf_start_text_sensor_->state != buf)
        this->pf_start_text_sensor_->publish_state(buf);
    }
    if ((prev == SCREEN_PF_CYCLES || prev == SCREEN_PF_CYCLES_EDIT) && screen == SCREEN_PF_DURATION &&
        this->pf_cycles_sensor_ != nullptr)
      this->pf_cycles_sensor_->publish_state(prev_value);
    if (prev == SCREEN_PF_DURATION && screen == SCREEN_PF && this->pf_duration_sensor_ != nullptr)
      this->pf_duration_sensor_->publish_state(prev_value);
    // Language: Jets 1 on the LANG edit screen saves the language shown (from the display
    // screen it keeps the current one, which f15 does not show).
    if (prev == SCREEN_LANG_EDIT && screen == SCREEN_LANG)
      this->publish_language_(prev_value);
    // Filter Change: Jets 1 on an FC value screen saves it; 128 = Off (2026-10-22/25).
    if ((prev == SCREEN_FC_SHOW || prev == SCREEN_FC_EDIT) && screen == SCREEN_FC && prev_value == 128) {
      this->fc_zero_kind_ = 1;
      this->publish_counters_();
    }
  }
  // The value byte can lag the screen code by a frame; take the first value that fits the key.
  if (this->uv_display_pending_ && screen == SCREEN_UV_DAYS) {
    const int days = uv_days_from_(value, this->screen_key_);
    if (days != -1) {
      this->publish_uv_days_(days);
      this->uv_display_pending_ = false;
    }
  }
  if (screen != SCREEN_UV_DAYS)
    this->uv_display_pending_ = false;
  this->menu_prev_screen_ = screen;
  this->menu_prev_value_ = value;
}

// ---------------------------------------------------------------- clock / date sync
// Topside screen codes (f13) and the path TIME and DATE are edited along (2026-10-22 capture):
//   main 78 -Menu-> 73 PF -> 64 HEAT -> 95 FC -> 90 UV -> 89 H2O -> 84 TIME -> 81 DATE
//   TIME 84 -Warmer-> 85 hour -Jets1-> 83 minute -Jets1-> 80 AM/PM (f15 1=PM) -Jets1-> 84
//   DATE 81 -Warmer-> 110 -Warmer-> 111 year (f15=yy) -Jets1-> 108 month -Jets1-> 109 day -Jets1-> 81
// Every press is sent only when the last reported screen is the one the step expects; any
// mismatch, timeout or topside press aborts without further presses (the menu then times out
// on its own without saving).
static const int NAV_TO_TIME[] = {73, 64, 95, 90, 89, 84};
static const uint32_t SYNC_STEP_TIMEOUT_MS = 3000;
static const uint32_t SYNC_BLIND_PRESS_MS = 600;
// The spa clock stands still from entering the time edit until the save (~3.6 s in the 26 and 27 Oct
// syncs) and keeps its seconds; only a changed value is applied (an unchanged save, 26 Oct 07:55, left
// the offset as it was). So a save moves the offset by 60 s per minute changed minus the edit time.
static const int SYNC_EDIT_PAUSE_S = 4;
// Auto sync from this offset (owner, 2026-11-03: was 60 s). The clock moves only in whole minutes
// minus the edit time, so in practice it is corrected from about +35 / -31 s (see best_minute_move_()).
static const int SYNC_AUTO_LIMIT_S = 30;
// A move is made only when it brings the offset at least this much nearer to 0 (the 27 Oct manual sync
// turned -4 s into +52 s).
static const int SYNC_MIN_GAIN_S = 5;

// Whole minutes to move the spa clock so that the offset after the save is nearest to 0, or 0 when no
// move improves it by SYNC_MIN_GAIN_S; *expect gets the offset expected after the save.
static int best_minute_move_(int off, int *expect) {
  const int delta = static_cast<int>(std::lround((SYNC_EDIT_PAUSE_S - off) / 60.0));
  const int e = off + 60 * delta - SYNC_EDIT_PAUSE_S;
  if (delta == 0 || std::abs(e) > std::abs(off) - SYNC_MIN_GAIN_S)
    return 0;
  *expect = e;
  return delta;
}

enum SyncStep : int {
  SYNC_IDLE = 0,
  SYNC_WAIT_MAIN,
  SYNC_NAV,
  SYNC_TIME_ENTER,
  SYNC_HOUR,
  SYNC_TO_MINUTE,
  SYNC_MINUTE,
  SYNC_TO_AMPM,
  SYNC_AMPM,
  SYNC_TIME_SAVE,
  SYNC_TO_DATE,
  SYNC_DATE_ENTER,
  SYNC_DATE_YEAR_ENTER,
  SYNC_YEAR,
  SYNC_MONTH,
  SYNC_DAY,
  SYNC_WAIT_EXIT,
  // read-only menu walks (nightly routine / Read Spa Settings button)
  READ_WAIT_MAIN,
  READ_UV_NAV,
  READ_UV_SHOW,
  READ_PF_NAV,
  READ_PF_SHOW,
  READ_PF_CYCLES,
  READ_PF_DURATION,
  READ_PF_SAVE,
  READ_LANG_NAV,
  READ_LANG_SHOW,
  TASK_WAIT_MAIN,
  READ_FC_NAV,
  READ_FC_SHOW,
  // settings write (request_setting_write)
  WRITE_WAIT_MAIN,
  WRITE_NAV,
  WRITE_SHOW,
  WRITE_ADJUST,
  WRITE_CONFIRM,
  WRITE_SAVE,
  WRITE_TEMP_SHOW,
  WRITE_TEMP_SWITCH,
  LOCK_PICK,  // on the LOCK menu: Warmer until the wanted lock option is shown
  LOCK_SAVE,  // Jets 1 sent: wait for the spa to show the lock
};

static const int NAV_TO_UV[] = {73, 64, 95, 90};
static const uint32_t READ_DWELL_MS = 1500;  // let f15 settle before confirming a PF value
static const int NAV_TO_LANG[] = {73, 64, 95, 90, 89, 84, 81, 106, 105};
static const int NAV_TO_FC[] = {73, 64, 95};
static const int TASK_SYNC = 1, TASK_UV = 2, TASK_PF = 4, TASK_LANG = 8, TASK_FC = 16;

// ---------------------------------------------------------------- settings write
// Menu screens (normalized) of the settings written from HA; see the README §9 table.
static const int NAV_ALL[] = {73, 64, 95, 90, 89, 84, 81, 106, 105};  // PF HEAT FC UV H2O TIME DATE TEMP LANG
static const int NAV_ALL_COUNT = sizeof(NAV_ALL) / sizeof(NAV_ALL[0]);
// ... and on to LOCK (100): recorded at key 117 on 2026-11-08 (raw f13 17 = LOCK, 16 Access, 23 Settings, 122 Temperature,
// 120 Service). The first Warmer on LOCK goes straight to the Access option; Jets 1 saves and returns to the main screen.
static const int NAV_LOCK[] = {73, 64, 95, 90, 89, 84, 81, 106, 105, 100};
static const int NAV_LOCK_COUNT = sizeof(NAV_LOCK) / sizeof(NAV_LOCK[0]);
static int lock_screen_(int mode) {
  return mode == 1 ? SCREEN_LOCK_AL : mode == 2 ? SCREEN_LOCK_TL : mode == 3 ? SCREEN_LOCK_SL : SCREEN_LOCK_SVL;
}
static bool is_lock_option_(int screen) {
  return screen == SCREEN_LOCK_AL || screen == SCREEN_LOCK_SL || screen == SCREEN_LOCK_TL || screen == SCREEN_LOCK_SVL;
}
static const int SCREEN_HEAT = 64, SCREEN_HEAT_SHOW = 65, SCREEN_HEAT_EDIT = 94;
static const int SCREEN_H2O = 89, SCREEN_H2O_SHOW = 86, SCREEN_H2O_EDIT = 87;
static const int SCREEN_TEMP = 106, SCREEN_TEMP_SHOW = 107, SCREEN_TEMP_EDIT = 104;
enum WriteKind : int {
  WRITE_NONE = 0,
  WRITE_HEAT = 1,
  WRITE_PF_START = 2,
  WRITE_PF_CYCLES = 3,
  WRITE_PF_DURATION = 4,
  WRITE_FC = 5,
  WRITE_UV = 6,
  WRITE_H2O = 7,
  WRITE_TEMP_UNIT = 8,
  WRITE_LANG = 9,
  WRITE_PF = 10,  // internal: one walk for start, cycles and duration
  WRITE_LOCK = 11,  // engage a lock: LOCK menu, option, Jets 1
};
static const uint32_t WRITE_CHANGE_TIMEOUT_MS = 2500;  // f15 must follow a Warmer/Cooler press by then

// f15 code of a ClearRay day count (Off = 128), as on the UV value screens.
static int uv_code_(int days) { return days == 0 ? 128 : ((days & 0xFF) ^ (days >> 8)); }

// Ordered f15 codes one press steps through, all treated as cyclic. Which button moves forward is
// learned on the first press (write_dir_); a list end that does not wrap is found by "no change".
static std::vector<int> write_codes_(int kind, int field) {
  std::vector<int> v;
  switch (kind) {
    case WRITE_HEAT:
      return {0, 1, 2};  // Auto, Eco, Day
    case WRITE_LANG:
      return {0, 1, 2, 3};
    case WRITE_PF:
      if (field == 0) {
        for (int h = 0; h < 24; h++)
          v.push_back(h);
        return v;
      }
      if (field == 1)
        return {1, 2, 3, 4, 8};
      return {1, 2, 3};
    case WRITE_FC:
      v.push_back(128);
      for (int d = 10; d <= 180; d += 10)
        v.push_back(d);
      return v;
    case WRITE_UV:
      for (int d = 10; d <= 360; d += 10)
        v.push_back(uv_code_(d));
      v.push_back(uv_code_(365));
      v.push_back(128);
      return v;
    case WRITE_H2O:
      return {128, 30, 60, 90, 120, 150, 180};
    default:
      return v;
  }
}

static int index_of_(const std::vector<int> &v, int code) {
  for (size_t i = 0; i < v.size(); i++)
    if (v[i] == code)
      return static_cast<int>(i);
  return -1;
}

// +1 if the shorter way from a to b in a cyclic list of n is forward, -1 if backward.
static int cyclic_dir_(int a, int b, int n) {
  const int fwd = ((b - a) % n + n) % n;
  return fwd <= n - fwd ? 1 : -1;
}

static int write_menu_screen_(int kind) {
  switch (kind) {
    case WRITE_HEAT:
      return SCREEN_HEAT;
    case WRITE_PF:
      return SCREEN_PF;
    case WRITE_FC:
      return SCREEN_FC;
    case WRITE_UV:
      return SCREEN_UV;
    case WRITE_H2O:
      return SCREEN_H2O;
    case WRITE_TEMP_UNIT:
      return SCREEN_TEMP;
    case WRITE_LANG:
      return SCREEN_LANG;
    case WRITE_LOCK:
      return SCREEN_LOCK;
    default:
      return -1;
  }
}

// Value screens of a field: the first Warmer shows the value, further presses change it.
static bool write_on_field_screen_(int kind, int field, int screen) {
  switch (kind) {
    case WRITE_HEAT:
      return screen == SCREEN_HEAT_SHOW || screen == SCREEN_HEAT_EDIT;
    case WRITE_LANG:
      return screen == SCREEN_LANG_SHOW || screen == SCREEN_LANG_EDIT;
    case WRITE_FC:
      return screen == SCREEN_FC_SHOW || screen == SCREEN_FC_EDIT;
    case WRITE_UV:
      return screen == SCREEN_UV_DAYS || screen == SCREEN_UV_EDIT;
    case WRITE_H2O:
      return screen == SCREEN_H2O_SHOW || screen == SCREEN_H2O_EDIT;
    case WRITE_PF:
      if (field == 0)
        return screen == SCREEN_PF_START || screen == SCREEN_PF_START_EDIT;
      if (field == 1)
        return screen == SCREEN_PF_CYCLES || screen == SCREEN_PF_CYCLES_EDIT;
      return screen == SCREEN_PF_DURATION;
    default:
      return false;
  }
}

static const char *write_name_(int kind) {
  switch (kind) {
    case WRITE_HEAT:
      return "Heater Mode";
    case WRITE_PF:
    case WRITE_PF_START:
    case WRITE_PF_CYCLES:
    case WRITE_PF_DURATION:
      return "Primary Filter";
    case WRITE_FC:
      return "Filter Change";
    case WRITE_UV:
      return "ClearRay Bulb";
    case WRITE_H2O:
      return "Water Refresh";
    case WRITE_TEMP_UNIT:
      return "Temperature Format";
    case WRITE_LANG:
      return "Language";
    case WRITE_LOCK:
      return "Lock";
    default:
      return "Setting";
  }
}

void SundanceSpa::request_setting_write(int kind, int value) {
  bool ok = false;
  switch (kind) {
    case WRITE_HEAT:
      ok = value >= 0 && value <= 2;
      break;
    case WRITE_PF_START:
      ok = value >= 0 && value <= 23;
      break;
    case WRITE_PF_CYCLES:
      ok = value == 1 || value == 2 || value == 3 || value == 4 || value == 8;
      break;
    case WRITE_PF_DURATION:
      ok = value >= 1 && value <= 3;
      break;
    case WRITE_FC:
      ok = value == 0 || (value >= 10 && value <= 180 && value % 10 == 0);
      break;
    case WRITE_UV:
      ok = value == 0 || value == 365 || (value >= 10 && value <= 360 && value % 10 == 0);
      break;
    case WRITE_H2O:
      ok = value == 0 || (value >= 30 && value <= 180 && value % 30 == 0);
      break;
    case WRITE_TEMP_UNIT:
      ok = value == 0 || value == 1;
      break;
    case WRITE_LANG:
      // Disabled (p31b): the ESP's presses do not change the panel language, and the code mapping per key
      // is not settled. Re-enable (ok = value >= 0 && value <= 3) once both are understood.
      ESP_LOGW(TAG, "Language write is disabled");
      break;
    default:
      break;
  }
  if (!ok) {
    ESP_LOGW(TAG, "Setting write %d: value %d not allowed", kind, value);
    return;
  }
  if (this->sl_on_ || this->svl_on_) {
    this->decoder_notice_(std::string(write_name_(kind)) + " not changed: the panel is locked");
    return;
  }
  ESP_LOGI(TAG, "Setting write queued: %s = %d", write_name_(kind), value);
  this->write_pending_[kind] = value;
  if (this->sync_step_ == SYNC_IDLE)
    this->start_next_task_();
}

// Starts the next pending write (writes run before reads); false when there is none.
bool SundanceSpa::start_next_write_() {
  for (int guard = 0; guard < 10; guard++) {
    int kind = WRITE_NONE;
    for (int k : {WRITE_HEAT, WRITE_TEMP_UNIT, WRITE_PF_START, WRITE_PF_CYCLES, WRITE_PF_DURATION, WRITE_FC,
                  WRITE_UV, WRITE_H2O, WRITE_LANG, WRITE_LOCK}) {
      if (this->write_pending_[k] != -1) {
        kind = k;
        break;
      }
    }
    if (kind == WRITE_NONE)
      return false;
    this->write_target_[0] = this->write_target_[1] = this->write_target_[2] = -1;
    if (kind == WRITE_PF_START || kind == WRITE_PF_CYCLES || kind == WRITE_PF_DURATION) {
      this->write_kind_ = WRITE_PF;
      this->write_target_[0] = this->write_pending_[WRITE_PF_START];
      this->write_target_[1] = this->write_pending_[WRITE_PF_CYCLES];
      this->write_target_[2] = this->write_pending_[WRITE_PF_DURATION];
      this->write_pending_[WRITE_PF_START] = this->write_pending_[WRITE_PF_CYCLES] =
          this->write_pending_[WRITE_PF_DURATION] = -1;
      // 8 cycles a day allow only 1 h (the duration screen offers nothing else then).
      if (this->write_target_[1] == 8)
        this->write_target_[2] = 1;
    } else {
      const int v = this->write_pending_[kind];
      this->write_pending_[kind] = -1;
      this->write_kind_ = kind;
      if (kind == WRITE_UV)
        this->write_target_[0] = uv_code_(v);
      else if (kind == WRITE_FC || kind == WRITE_H2O)
        this->write_target_[0] = v == 0 ? 128 : v;
      else if (kind == WRITE_LANG)
        this->write_target_[0] = lang_code_(v, this->screen_key_);
      else
        this->write_target_[0] = v;
      if (kind == WRITE_TEMP_UNIT && (v == 1) == this->spa_fahrenheit_) {
        ESP_LOGI(TAG, "Temperature Format already as requested");
        continue;
      }
      if (kind == WRITE_HEAT && this->prior_status_.size() >= 16) {
        const char *cur = heater_mode_name_(this->prior_status_[6]);
        const char *want = v == 0 ? "Auto" : v == 1 ? "Eco" : "Day";
        if (cur != nullptr && std::string(cur) == want) {
          ESP_LOGI(TAG, "Heater Mode already %s", want);
          continue;
        }
      }
    }
    this->write_field_ = 0;
    this->write_presses_ = 0;
    this->write_no_change_ = 0;
    this->write_dir_ = 1;
    // LANG: the display screen's f15 0 is not a language, so it is treated like an off-list value.
    this->write_offgrid_ = (this->write_kind_ == WRITE_UV && this->uv_days_ > 0 && this->uv_days_ % 10 != 0 &&
                            this->uv_days_ != 365) ||
                           this->write_kind_ == WRITE_LANG;
    this->write_verify_kind_ = this->write_kind_;
    if (this->write_kind_ == WRITE_LOCK)
      this->lock_mode_ = this->write_target_[0];
    this->write_verify_value_ = this->write_kind_ == WRITE_PF ? -1 : this->write_target_[0];
    ESP_LOGI(TAG, "Changing %s from the topside menu", write_name_(this->write_kind_));
    this->sync_goto_(WRITE_WAIT_MAIN);
    return true;
  }
  return false;
}

// One Warmer or Cooler press from the shown f15 code toward the field's target, the shorter way round.
void SundanceSpa::write_step_toward_(int value) {
  const std::vector<int> codes = write_codes_(this->write_kind_, this->write_field_);
  const int n = static_cast<int>(codes.size());
  // A ClearRay count off the 10-day grid (e.g. 317) can share its code with a grid value (60):
  // treat the first value as off the list.
  const int cur = (this->write_offgrid_ && this->write_presses_ == 0) ? -1 : index_of_(codes, value);
  const int tgt = index_of_(codes, this->write_target_[this->write_field_]);
  // Off the list (e.g. ClearRay 317 between steps of 10): one press lands on the grid first.
  const int step = (cur >= 0 && tgt >= 0) ? cyclic_dir_(cur, tgt, n) : 1;
  const bool warmer = (step > 0) == (this->write_dir_ > 0);
  this->write_last_value_ = value;
  this->write_presses_++;
  this->sync_press_(warmer ? BTN_TEMP_UP : BTN_TEMP_DOWN);
  this->sync_goto_(WRITE_ADJUST);
}

void SundanceSpa::service_write_(int screen, int value, uint32_t now) {
  const int menu = write_menu_screen_(this->write_kind_);
  switch (this->sync_step_) {
    case WRITE_WAIT_MAIN: {
      const bool busy = now - this->last_panel_press_ms_ < 6000 || this->wanted_jet1_ != NO_CHANGE_REQUESTED ||
                        this->wanted_temp_ != NO_CHANGE_REQUESTED || this->wanted_light680_ != NO_CHANGE_REQUESTED;
      if (screen != SCREEN_MAIN || busy) {
        if (now - this->sync_step_ms_ > 60000)
          this->abort_clock_sync_("spa not idle on the main screen");
        return;
      }
      this->sync_started_ms_ = now;
      this->sync_nav_index_ = 0;
      this->sync_press_(BTN_MENU);
      this->sync_goto_(WRITE_NAV);
      return;
    }
    case WRITE_NAV: {
      const bool lock = this->write_kind_ == WRITE_LOCK;
      const int *nav = lock ? NAV_LOCK : NAV_ALL;
      const int nav_count = lock ? NAV_LOCK_COUNT : NAV_ALL_COUNT;
      const int expect = nav[this->sync_nav_index_];
      if (screen == expect) {
        if (expect == menu) {
          this->sync_press_(BTN_TEMP_UP);  // the first press only shows the current value (LOCK: the Access option)
          this->sync_goto_(lock ? LOCK_PICK : this->write_kind_ == WRITE_TEMP_UNIT ? WRITE_TEMP_SHOW : WRITE_SHOW);
        } else if (this->sync_nav_index_ + 1 < nav_count) {
          this->sync_nav_index_++;
          this->sync_press_(BTN_MENU);
          this->sync_step_ms_ = now;
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("menu navigation");
      }
      return;
    }
    case WRITE_SHOW: {
      // The field's value is on screen: keep it, or start stepping toward the target.
      if (!write_on_field_screen_(this->write_kind_, this->write_field_, screen)) {
        if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS + READ_DWELL_MS)
          this->abort_clock_sync_("value screen not shown");
        return;
      }
      if (now - this->sync_press_ms_ < READ_DWELL_MS)
        return;  // let f15 settle
      const int target = this->write_target_[this->write_field_];
      if (target == -1 || (value == target && this->write_kind_ != WRITE_LANG)) {
        if (this->write_kind_ != WRITE_PF) {
          ESP_LOGI(TAG, "%s already as requested", write_name_(this->write_kind_));
          this->write_verify_kind_ = WRITE_NONE;
          this->sync_goto_(TASK_WAIT_MAIN);  // no further presses: the menu times out unsaved
          return;
        }
        this->sync_goto_(WRITE_CONFIRM);
        return;
      }
      this->write_step_toward_(value);
      return;
    }
    case WRITE_ADJUST: {
      const int target = this->write_target_[this->write_field_];
      const bool on_screen = write_on_field_screen_(this->write_kind_, this->write_field_, screen);
      // LANG: the first press leaves the display screen for the edit screen, even if f15 stays the same.
      const bool lang_entered = this->write_kind_ == WRITE_LANG && this->write_presses_ == 1 && screen == SCREEN_LANG_EDIT;
      if (on_screen && (value != this->write_last_value_ || lang_entered) && now - this->sync_press_ms_ >= 300) {
        const std::vector<int> codes = write_codes_(this->write_kind_, this->write_field_);
        const int n = static_cast<int>(codes.size());
        const int a = (this->write_offgrid_ && this->write_presses_ == 1) ? -1 : index_of_(codes, this->write_last_value_);
        const int b = index_of_(codes, value);
        const int t = index_of_(codes, target);
        // Moved the other way from the one intended: the Warmer/Cooler mapping is reversed here.
        if (a >= 0 && b >= 0 && t >= 0 && cyclic_dir_(a, b, n) != cyclic_dir_(a, t, n)) {
          this->write_dir_ = -this->write_dir_;
          ESP_LOGI(TAG, "%s: Warmer/Cooler direction reversed", write_name_(this->write_kind_));
        }
        this->write_no_change_ = 0;
        if (value == target) {
          this->sync_goto_(WRITE_CONFIRM);
          return;
        }
        if (this->write_presses_ >= n * 2 + 2) {
          this->abort_clock_sync_("value did not reach the target");
          return;
        }
        this->write_step_toward_(value);
        return;
      }
      if (now - this->sync_press_ms_ > WRITE_CHANGE_TIMEOUT_MS) {
        // No change: perhaps the end of a list that does not wrap - try the other button once.
        if (++this->write_no_change_ >= 2) {
          this->abort_clock_sync_("value does not change");
          return;
        }
        this->write_dir_ = -this->write_dir_;
        this->write_step_toward_(on_screen ? value : this->write_last_value_);
      }
      return;
    }
    case WRITE_CONFIRM: {
      // Jets 1 saves the value shown - only once it is exactly the target and has settled.
      if (!write_on_field_screen_(this->write_kind_, this->write_field_, screen)) {
        if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS)
          this->abort_clock_sync_("left the value screen");
        return;
      }
      const int target = this->write_target_[this->write_field_];
      if (target != -1 && value != target) {
        this->write_step_toward_(value);
        return;
      }
      if (now - this->sync_press_ms_ < READ_DWELL_MS)
        return;
      this->sync_press_(BTN_P1);
      if (this->write_kind_ == WRITE_PF && this->write_field_ < 2) {
        this->write_field_++;
        this->write_presses_ = 0;
        this->write_no_change_ = 0;
        this->sync_goto_(WRITE_SHOW);  // the next PF screen shows its value right away
      } else {
        this->sync_goto_(WRITE_SAVE);
      }
      return;
    }
    case WRITE_SAVE: {
      if (screen == menu) {
        ESP_LOGI(TAG, "%s saved", write_name_(this->write_kind_));
        this->sync_goto_(TASK_WAIT_MAIN);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("save not confirmed");
      }
      return;
    }
    case WRITE_TEMP_SHOW: {
      // TEMP: Warmer shows the unit (107), a second Warmer switches it (104), Jets 1 saves (106).
      if (screen == SCREEN_TEMP_SHOW) {
        if (now - this->sync_press_ms_ >= READ_DWELL_MS) {
          this->sync_press_(BTN_TEMP_UP);
          this->sync_goto_(WRITE_TEMP_SWITCH);
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS + READ_DWELL_MS) {
        this->abort_clock_sync_("TEMP screen");
      }
      return;
    }
    case WRITE_TEMP_SWITCH: {
      if (screen == SCREEN_TEMP_EDIT) {
        if (now - this->sync_press_ms_ >= READ_DWELL_MS) {
          this->sync_press_(BTN_P1);
          this->sync_goto_(WRITE_SAVE);
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS + READ_DWELL_MS) {
        this->abort_clock_sync_("TEMP switch");
      }
      return;
    }
    case LOCK_PICK: {
      const int target = lock_screen_(this->lock_mode_);
      if (screen == target) {
        if (now - this->sync_press_ms_ >= READ_DWELL_MS) {
          // Our own Jets 1 saves the lock. It is not a panel event, so register it like one: the f14 flip that follows is
          // then accepted as the Settings / Service Lock confirmation.
          if (this->lock_mode_ == 3 || this->lock_mode_ == 4) {
            this->lock_event_ = this->lock_mode_ == 3 ? 1 : 2;
            this->lock_event_ms_ = now;
            this->lock_event_f14_ = this->prior_status_[14];
          }
          this->sync_press_(BTN_P1);
          this->sync_goto_(LOCK_SAVE);
        }
      } else if (is_lock_option_(screen)) {
        if (now - this->sync_press_ms_ >= READ_DWELL_MS) {
          if (++this->write_presses_ > 5) {
            this->abort_clock_sync_("lock option not found");
            return;
          }
          this->sync_press_(BTN_TEMP_UP);
          this->sync_step_ms_ = now;
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS + READ_DWELL_MS) {
        this->abort_clock_sync_("LOCK menu");
      }
      return;
    }
    case LOCK_SAVE: {
      const bool al = (this->prior_status_[1] & 0x08) != 0;
      const bool tl = (this->prior_status_[10] & 0x10) != 0;
      const bool done = this->lock_mode_ == 1   ? al
                        : this->lock_mode_ == 2 ? tl
                        : this->lock_mode_ == 3 ? this->sl_on_
                                                : this->svl_on_;
      if (done) {
        ESP_LOGI(TAG, "Lock %d set", this->lock_mode_);
        this->sync_goto_(TASK_WAIT_MAIN);
      } else if (now - this->sync_step_ms_ > 4000) {
        this->abort_clock_sync_("not confirmed by the spa");
      }
      return;
    }
    default:
      return;
  }
}

// ---------------------------------------------------------------- lock from Home Assistant
// Off = one "Menu held" frame (209): the controller accepts it from our channel for all four locks (verified
// 2026-11-08). It is not a panel event, so the tracked Settings / Service flags are cleared here, unconditionally.
void SundanceSpa::release_locks_() {
  this->queue_press_(BTN_MENU_HOLD);
  this->lock_event_ = 0;
  if (this->sl_on_ || this->svl_on_) {
    this->sl_on_ = false;
    this->svl_on_ = false;
    this->svl_since_ms_ = 0;
    const uint8_t locks = 0;
    this->lock_pref_.save(&locks);
  }
  ESP_LOGI(TAG, "Lock: released (Menu held sent)");
}

void SundanceSpa::queue_lock_walk_(int mode) {
  ESP_LOGI(TAG, "Lock walk queued: mode %d", mode);
  this->write_pending_[WRITE_LOCK] = mode;
  if (this->sync_step_ == SYNC_IDLE)
    this->start_next_task_();
}

const char *SundanceSpa::lock_blocking_(int use) const {
  const auto &d = this->prior_status_;
  if (d.size() >= 16) {
    if (d[1] & 0x08)  // Access Lock: every function
      return "Access Lock";
    if ((d[10] & 0x10) && use == LOCKUSE_TEMP)  // Temperature Lock: only the set point
      return "Temperature Lock";
  }
  if (this->svl_on_)  // Service Lock: every function
    return "Service Lock";
  return nullptr;
}

void SundanceSpa::request_lock(int mode) {
  if (mode < 0 || mode > 4 || this->prior_status_.size() < 16) {
    ESP_LOGW(TAG, "Lock %d not allowed now", mode);
    return;
  }
  const bool al = (this->prior_status_[1] & 0x08) != 0;
  const bool tl = (this->prior_status_[10] & 0x10) != 0;
  const int current = al ? 1 : tl ? 2 : this->sl_on_ ? 3 : this->svl_on_ ? 4 : 0;
  if (mode == current) {
    ESP_LOGI(TAG, "Lock already as requested");
    return;
  }
  if (mode == 0) {
    this->release_locks_();
    return;
  }
  if (current != 0) {
    // One lock at a time: release the active one, give the controller a moment, then walk the menu.
    this->release_locks_();
    this->set_timeout("lock_engage", 4000, [this, mode]() { this->queue_lock_walk_(mode); });
    return;
  }
  this->queue_lock_walk_(mode);
}


bool SundanceSpa::clock_sync_active_() const { return this->sync_step_ != SYNC_IDLE; }

// True once a menu task has started pressing buttons. While a task is still waiting for the main
// screen, HA commands (lights, jets, temperature) must run first — the task waits for them to finish;
// blocking them here deadlocked both for a minute per queued task (2026-10-24).
bool SundanceSpa::menu_in_use_() const {
  return this->sync_step_ != SYNC_IDLE && this->sync_step_ != SYNC_WAIT_MAIN && this->sync_step_ != READ_WAIT_MAIN &&
         this->sync_step_ != WRITE_WAIT_MAIN;
}

void SundanceSpa::set_clock_sync_status_(const std::string &s) {
  ESP_LOGI(TAG, "Clock sync: %s", s.c_str());
  if (this->clock_sync_text_sensor_ != nullptr)
    this->clock_sync_text_sensor_->publish_state(s);
  if (s.rfind("Failed", 0) == 0)
    this->decoder_notice_("Clock sync " + s);
}

void SundanceSpa::decoder_notice_(const std::string &s) {
  ESP_LOGW(TAG, "Decoder notice: %s", s.c_str());
  if (this->decoder_notice_text_sensor_ == nullptr)
    return;
  this->decoder_notice_text_sensor_->publish_state(s);
  this->set_timeout("decoder_notice_clear", 3600000,
                    [this]() { this->decoder_notice_text_sensor_->publish_state("OK"); });
}

void SundanceSpa::request_clock_sync(bool manual) {
  if (this->time_ == nullptr || !this->time_->now().is_valid()) {
    this->set_clock_sync_status_("Failed: no time from Home Assistant");
    this->start_next_task_();
    return;
  }
  if (this->clock_sync_active_()) {
    ESP_LOGW(TAG, "Clock sync already running");
    return;
  }
  this->sync_manual_ = manual;
  // With the offset known to the second, move the clock by whole minutes so that offset + 60*delta -
  // edit time is nearest to 0, when that is worth it (best_minute_move_()). Without it (just after boot
  // or a sync) aim at HA's time.
  this->sync_by_offset_ = this->spa_offset_valid_;
  this->sync_delta_min_ = 0;
  if (this->sync_by_offset_) {
    const int off = this->spa_offset_s_;
    this->sync_delta_min_ = best_minute_move_(off, &this->sync_expect_s_);
    auto t = this->time_->now();
    const bool date_ok = this->prior_status_.size() >= 16 &&
                         this->prior_status_[7] == encode_spa_date_(t.day_of_month, t.month) &&
                         this->prior_status_[9] == ((t.year - 2000) ^ 1);
    if (this->sync_delta_min_ == 0 && date_ok) {
      char buf[48];
      snprintf(buf, sizeof(buf), "OK (already within %+d s)", off);
      this->last_sync_attempt_ms_ = millis();
      this->set_clock_sync_status_(buf);
      this->start_next_task_();
      return;
    }
    if (this->sync_delta_min_ == 0)
      this->sync_expect_s_ = off;
  }
  this->spa_offset_valid_ = false;  // re-measured at the first rollover after the sync
  this->sync_eval_pending_ = true;
  this->sync_step_ = SYNC_WAIT_MAIN;
  this->sync_step_ms_ = millis();
  this->set_clock_sync_status_("Waiting for main screen");
}

void SundanceSpa::abort_clock_sync_(const char *why) {
  const bool was_sync = this->sync_step_ < READ_WAIT_MAIN;
  const bool was_write = this->sync_step_ >= WRITE_WAIT_MAIN;
  this->sync_step_ = SYNC_IDLE;
  this->last_sync_attempt_ms_ = millis();
  if (was_sync) {
    this->set_clock_sync_status_(std::string("Failed: ") + why);
  } else if (was_write) {
    // The menu times out without saving what was not confirmed; re-read so HA shows the spa's value.
    this->write_verify_kind_ = WRITE_NONE;
    this->decoder_notice_(std::string(write_name_(this->write_kind_)) + " not changed: " + why);
    if (this->write_kind_ == WRITE_PF)
      this->routine_mask_ |= TASK_PF;
    else if (this->write_kind_ == WRITE_UV)
      this->routine_mask_ |= TASK_UV;
    else if (this->write_kind_ == WRITE_LANG)
      this->routine_mask_ |= TASK_LANG;
  } else {
    this->read_failed_ = true;
    this->decoder_notice_(std::string("Settings read aborted: ") + why);
  }
  this->start_next_task_();
}

bool SundanceSpa::clock_needs_sync_() const {
  if (this->time_ == nullptr || this->prior_status_.size() < 16)
    return false;
  auto t = this->time_->now();
  if (!t.is_valid())
    return false;
  const int ha_min = t.hour * 60 + t.minute;
  const int spa_min = this->spa_hour_ * 60 + this->spa_minute_;
  int drift = std::abs(ha_min - spa_min);
  drift = std::min(drift, 1440 - drift);
  const bool date_ok = this->prior_status_[7] == encode_spa_date_(t.day_of_month, t.month) &&
                       this->prior_status_[9] == ((t.year - 2000) ^ 1);
  // The spa only reports whole minutes, but the moment its minute rolls over gives its offset to the
  // second (see track_spa_offset_()). Sync from SYNC_AUTO_LIMIT_S when a whole-minute move actually
  // improves it (otherwise every check would end in "already within"), or from 120 s after two syncs in a
  // row did not get it under a minute.
  // Until a rollover has been seen (just after boot or a sync) fall back to whole minutes, 1 min slack.
  if (this->spa_offset_valid_) {
    const int off = this->spa_offset_s_;
    const int limit = this->sync_fail_streak_ >= 2 ? 120 : SYNC_AUTO_LIMIT_S;
    int expect = 0;
    return (std::abs(off) >= limit && best_minute_move_(off, &expect) != 0) || !date_ok;
  }
  return drift > 1 || !date_ok;
}

// Runs the queued tasks one after another: clock sync, then UV, PF and language reads.
void SundanceSpa::start_next_task_() {
  if (this->sync_step_ != SYNC_IDLE)
    return;
  if (this->routine_mask_ & TASK_SYNC) {
    this->routine_mask_ &= ~TASK_SYNC;
    this->request_clock_sync(false);
    return;
  }
  if (this->start_next_write_())
    return;
  if (this->routine_mask_ & (TASK_UV | TASK_PF | TASK_LANG | TASK_FC)) {
    this->read_task_ = (this->routine_mask_ & TASK_UV)     ? TASK_UV
                       : (this->routine_mask_ & TASK_PF)   ? TASK_PF
                       : (this->routine_mask_ & TASK_LANG) ? TASK_LANG
                                                            : TASK_FC;
    this->routine_mask_ &= ~this->read_task_;
    this->read_ran_ = true;
    this->sync_goto_(READ_WAIT_MAIN);
    ESP_LOGI(TAG, "Reading %s from the topside menu",
             this->read_task_ == TASK_UV     ? "ClearRay Bulb"
             : this->read_task_ == TASK_PF   ? "Primary Filter"
             : this->read_task_ == TASK_LANG ? "Language"
                                             : "Filter Change");
    return;
  }
  // Nothing left to run: if reads ran and none was aborted, stamp the time of this settings read.
  if (this->read_ran_) {
    if (!this->read_failed_ && this->last_settings_read_text_sensor_ != nullptr) {
      char buf[24];
      auto t = this->time_ != nullptr ? this->time_->now() : ESPTime{};
      if (t.is_valid())
        snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d", t.day_of_month, t.month, t.year, t.hour, t.minute);
      else
        snprintf(buf, sizeof(buf), "OK (no time)");
      this->last_settings_read_text_sensor_->publish_state(buf);
    }
    this->read_ran_ = false;
    this->read_failed_ = false;
  }
}

void SundanceSpa::request_settings_read(bool with_sync) {
  if (this->sync_step_ != SYNC_IDLE) {
    ESP_LOGW(TAG, "Menu task already running");
    return;
  }
  this->routine_mask_ = TASK_UV | TASK_PF | TASK_LANG;
  // FC at 0 is either Off or due; only the FC menu tells which.
  if (this->prior_status_.size() >= 16 && this->prior_status_[3] == 0xFA && this->fc_zero_kind_ == 0)
    this->routine_mask_ |= TASK_FC;
  if (with_sync && this->clock_needs_sync_())
    this->routine_mask_ |= TASK_SYNC;
  this->start_next_task_();
}

void SundanceSpa::sync_press_(uint8_t btn) {
  this->queue_press_(btn);
  this->sync_press_ms_ = millis();
}

void SundanceSpa::sync_goto_(int step) {
  this->sync_step_ = step;
  this->sync_step_ms_ = millis();
}

void SundanceSpa::service_clock_sync_() {
  if (this->sync_step_ == SYNC_IDLE || this->prior_status_.size() < 16 || this->channel_ < 0)
    return;
  const uint32_t now = millis();
  if (!this->tx_queue_.empty())
    return;
  const int screen = this->screen_();
  const int value = this->prior_status_[15];
  const bool panel_busy = now - this->last_panel_press_ms_ < 1500;

  if (this->sync_step_ != SYNC_WAIT_MAIN && this->sync_step_ != READ_WAIT_MAIN && this->sync_step_ != WRITE_WAIT_MAIN &&
      this->last_panel_press_ms_ > this->sync_started_ms_) {
    this->abort_clock_sync_("topside button pressed");
    return;
  }

  if (this->sync_step_ >= WRITE_WAIT_MAIN) {
    this->service_write_(screen, value, now);
    return;
  }

  switch (this->sync_step_) {
    case SYNC_WAIT_MAIN: {
      const bool busy = panel_busy || this->wanted_jet1_ != NO_CHANGE_REQUESTED ||
                        this->wanted_temp_ != NO_CHANGE_REQUESTED || this->wanted_light680_ != NO_CHANGE_REQUESTED;
      if (!(screen == SCREEN_MAIN) || busy || now - this->last_panel_press_ms_ < 6000) {
        if (now - this->sync_step_ms_ > 60000)
          this->abort_clock_sync_("spa not idle on the main screen");
        return;
      }
      this->sync_started_ms_ = now;
      this->sync_nav_index_ = 0;
      this->sync_press_(BTN_MENU);
      this->sync_goto_(SYNC_NAV);
      return;
    }
    case SYNC_NAV: {
      const int expect = NAV_TO_TIME[this->sync_nav_index_];
      if (screen == expect) {
        if (expect == SCREEN_TIME) {
          this->sync_press_(BTN_TEMP_UP);
          this->sync_goto_(SYNC_TIME_ENTER);
        } else {
          this->sync_nav_index_++;
          this->sync_press_(BTN_MENU);
          this->sync_step_ms_ = now;
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("menu navigation");
      }
      return;
    }
    case SYNC_TIME_ENTER: {
      if (screen == SCREEN_TIME_HOUR) {
        // The spa clock stands still from here to the save, so the time it shows now is what we edit.
        this->sync_edit_ms_ = now;
        auto t = this->time_->now();
        const int spa_tod = this->spa_hour_ * 60 + this->spa_minute_;
        int target_tod;
        if (this->sync_by_offset_) {
          target_tod = spa_tod + this->sync_delta_min_;
        } else {
          // No offset yet: the minute nearest to HA's time when the save will land (~edit time later).
          const int ha_s = t.hour * 3600 + t.minute * 60 + t.second + SYNC_EDIT_PAUSE_S;
          target_tod = (ha_s + 30) / 60;
        }
        target_tod = ((target_tod % 1440) + 1440) % 1440;
        const int tgt_h24 = target_tod / 60;
        // The hour field (1-12) wraps on its own as well (11 -> 12 -> 1 and back, AM/PM untouched; owner's
        // panel test 2026-10-27), so the short way round here too. AM/PM is set on its own screen.
        int hour_steps = ((tgt_h24 - this->spa_hour_) % 12 + 12) % 12;
        if (hour_steps > 6)
          hour_steps -= 12;
        this->sync_count_ = hour_steps;
        this->sync_target_pm_ = tgt_h24 >= 12 ? 1 : 0;
        this->sync_target_min_ = target_tod % 60;
        ESP_LOGI(TAG, "Clock sync: spa %02d:%02d -> %02d:%02d%s", this->spa_hour_, this->spa_minute_, tgt_h24,
                 this->sync_target_min_,
                 this->sync_by_offset_ ? "" : " (offset not measured yet: aiming at HA time)");
        this->sync_goto_(SYNC_HOUR);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("time menu");
      }
      return;
    }
    case SYNC_HOUR:
    case SYNC_MINUTE: {
      const int edit_screen = this->sync_step_ == SYNC_HOUR ? SCREEN_TIME_HOUR : SCREEN_TIME_MINUTE;
      // Once a value is changed the panel shows it on a different screen code (hour: 85 -> 82,
      // 2026-10-23); f2 bit5 stays set while a value is being edited, so accept either.
      if (screen != edit_screen && !this->menu_editing_()) {
        this->abort_clock_sync_("left the time edit screen");
        return;
      }
      if (now - this->sync_press_ms_ < SYNC_BLIND_PRESS_MS)
        return;
      if (this->sync_count_ > 0) {
        this->sync_press_(BTN_TEMP_UP);
        this->sync_count_--;
      } else if (this->sync_count_ < 0) {
        this->sync_press_(BTN_TEMP_DOWN);
        this->sync_count_++;
      } else {
        this->sync_press_(BTN_P1);  // confirm
        this->sync_goto_(this->sync_step_ == SYNC_HOUR ? SYNC_TO_MINUTE : SYNC_TO_AMPM);
      }
      return;
    }
    case SYNC_TO_MINUTE: {
      if (screen == SCREEN_TIME_MINUTE) {
        // Target minute was fixed with the hour (see SYNC_TIME_ENTER); the spa minute here is the one
        // the clock stood at when the edit began. The minute field wraps on its own without touching the
        // hour (59 -> 00 -> 59, owner's panel test 2026-10-27), so take the short way round.
        int steps = ((this->sync_target_min_ - this->spa_minute_) % 60 + 60) % 60;
        if (steps > 30)
          steps -= 60;
        this->sync_count_ = steps;
        this->sync_goto_(SYNC_MINUTE);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("minute screen");
      }
      return;
    }
    case SYNC_TO_AMPM: {
      if (screen == SCREEN_TIME_AMPM) {
        this->sync_goto_(SYNC_AMPM);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("AM/PM screen");
      }
      return;
    }
    case SYNC_AMPM: {
      if (screen != SCREEN_TIME_AMPM && !this->menu_editing_()) {
        this->abort_clock_sync_("left the AM/PM screen");
        return;
      }
      if (now - this->sync_press_ms_ < SYNC_BLIND_PRESS_MS)
        return;
      if (value != this->sync_target_pm_) {
        if (now - this->sync_press_ms_ < 2500 && this->sync_count_ > 0)
          return;  // waiting for the toggle to show
        if (++this->sync_count_ > 3) {
          this->abort_clock_sync_("AM/PM did not toggle");
          return;
        }
        this->sync_press_(BTN_TEMP_UP);
        return;
      }
      this->sync_press_(BTN_P1);  // save time
      if (this->sync_by_offset_)
        ESP_LOGI(TAG, "Clock sync: time edit took %.1f s, expecting an offset of about %+d s",
                 (now - this->sync_edit_ms_) / 1000.0f, this->sync_expect_s_);
      this->sync_goto_(SYNC_TIME_SAVE);
      return;
    }
    case SYNC_TIME_SAVE: {
      if (screen == SCREEN_TIME) {
        this->sync_press_(BTN_MENU);
        this->sync_goto_(SYNC_TO_DATE);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("saving time");
      }
      return;
    }
    case SYNC_TO_DATE: {
      if (screen == SCREEN_DATE) {
        this->sync_press_(BTN_TEMP_UP);
        this->sync_goto_(SYNC_DATE_ENTER);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("date menu");
      }
      return;
    }
    case SYNC_DATE_ENTER: {
      if (screen == SCREEN_DATE_SCROLL) {
        this->sync_press_(BTN_TEMP_UP);
        this->sync_goto_(SYNC_DATE_YEAR_ENTER);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("date display");
      }
      return;
    }
    case SYNC_DATE_YEAR_ENTER: {
      if (screen == SCREEN_DATE_YEAR) {
        this->sync_count_ = 0;
        this->sync_goto_(SYNC_YEAR);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("year screen");
      }
      return;
    }
    case SYNC_YEAR:
    case SYNC_MONTH:
    case SYNC_DAY: {
      // f15 shows the value being edited, so each step is verified before the next press.
      int edit_screen, target, next_screen, next_step;
      auto t = this->time_->now();
      if (this->sync_step_ == SYNC_YEAR) {
        edit_screen = SCREEN_DATE_YEAR;
        target = t.year - 2000;
        next_screen = SCREEN_DATE_MONTH;
        next_step = SYNC_MONTH;
      } else if (this->sync_step_ == SYNC_MONTH) {
        edit_screen = SCREEN_DATE_MONTH;
        target = t.month;
        next_screen = SCREEN_DATE_DAY;
        next_step = SYNC_DAY;
      } else {
        edit_screen = SCREEN_DATE_DAY;
        target = t.day_of_month;
        next_screen = SCREEN_DATE;
        next_step = SYNC_WAIT_EXIT;
      }
      if (screen == next_screen && this->sync_confirmed_) {
        this->sync_confirmed_ = false;
        this->sync_count_ = 0;
        this->sync_goto_(next_step);
        return;
      }
      if (screen != edit_screen && !(this->menu_editing_() && !this->sync_confirmed_)) {
        if (!this->sync_confirmed_ || now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS)
          this->abort_clock_sync_("left the date edit screen");
        return;
      }
      if (this->sync_confirmed_) {
        if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS)
          this->abort_clock_sync_("date confirm");
        return;
      }
      if (now - this->sync_press_ms_ < SYNC_BLIND_PRESS_MS)
        return;
      if (value == target) {
        this->sync_press_(BTN_P1);
        this->sync_confirmed_ = true;
        this->sync_step_ms_ = now;
        return;
      }
      if (++this->sync_count_ > 40) {
        this->abort_clock_sync_("date value did not converge");
        return;
      }
      this->sync_press_(value < target ? BTN_TEMP_UP : BTN_TEMP_DOWN);
      return;
    }
    case SYNC_WAIT_EXIT: {
      if ((screen == SCREEN_MAIN)) {
        this->sync_step_ = SYNC_IDLE;
        this->last_sync_attempt_ms_ = now;
        char buf[48];
        snprintf(buf, sizeof(buf), "OK %02d:%02d", this->spa_hour_, this->spa_minute_);
        this->set_clock_sync_status_(buf);
        this->start_next_task_();
      } else if (now - this->sync_step_ms_ > 60000) {  // DATE screen took ~25 s to time out in tests
        this->abort_clock_sync_("menu did not return to main screen");
      }
      return;
    }

    // ------------------------------------------------ read-only menu walks
    case READ_WAIT_MAIN: {
      const bool busy = panel_busy || this->wanted_jet1_ != NO_CHANGE_REQUESTED ||
                        this->wanted_temp_ != NO_CHANGE_REQUESTED || this->wanted_light680_ != NO_CHANGE_REQUESTED;
      if (!(screen == SCREEN_MAIN) || busy || now - this->last_panel_press_ms_ < 6000) {
        if (now - this->sync_step_ms_ > 60000)
          this->abort_clock_sync_("spa not idle on the main screen");
        return;
      }
      this->sync_started_ms_ = now;
      this->sync_nav_index_ = 0;
      this->sync_press_(BTN_MENU);
      this->sync_goto_(this->read_task_ == TASK_UV     ? READ_UV_NAV
                       : this->read_task_ == TASK_PF   ? READ_PF_NAV
                       : this->read_task_ == TASK_LANG ? READ_LANG_NAV
                                                       : READ_FC_NAV);
      return;
    }
    case READ_FC_NAV: {
      const int expect = NAV_TO_FC[this->sync_nav_index_];
      if (screen == expect) {
        if (expect == SCREEN_FC) {
          this->sync_press_(BTN_TEMP_UP);  // first press only displays the current value
          this->sync_goto_(READ_FC_SHOW);
        } else {
          this->sync_nav_index_++;
          this->sync_press_(BTN_MENU);
          this->sync_step_ms_ = now;
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("FC menu navigation");
      }
      return;
    }
    case READ_FC_SHOW: {
      if (screen == SCREEN_FC_SHOW || screen == SCREEN_FC_EDIT) {
        if (now - this->sync_step_ms_ > READ_DWELL_MS) {
          // Only needed while f3 says 0: 128 = Off, anything else = counter expired.
          this->fc_zero_kind_ = value == 128 ? 1 : 2;
          ESP_LOGI(TAG, "Filter Change menu value %d -> %s", value, value == 128 ? "Off" : "Change Now");
          this->publish_counters_();
          this->sync_goto_(TASK_WAIT_MAIN);  // no further presses: the menu times out unsaved
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("FC display");
      }
      return;
    }
    case READ_UV_NAV: {
      const int expect = NAV_TO_UV[this->sync_nav_index_];
      if (screen == expect) {
        if (expect == SCREEN_UV) {
          this->sync_press_(BTN_TEMP_UP);  // first press only displays the counter
          this->sync_goto_(READ_UV_SHOW);
        } else {
          this->sync_nav_index_++;
          this->sync_press_(BTN_MENU);
          this->sync_step_ms_ = now;
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("UV menu navigation");
      }
      return;
    }
    case READ_UV_SHOW: {
      const int days = screen == SCREEN_UV_DAYS ? uv_days_from_(value, this->screen_key_) : -1;
      if (days != -1) {
        this->publish_uv_days_(days);
        ESP_LOGI(TAG, "ClearRay Bulb: %d days", days);
        this->sync_goto_(TASK_WAIT_MAIN);  // no further presses: the menu times out unsaved
      } else if (screen != SCREEN_UV && screen != SCREEN_UV_DAYS && value != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ClearRay Bulb shown on unmapped screen %d with value %d", screen, value);
        this->decoder_notice_(buf);
        this->sync_goto_(TASK_WAIT_MAIN);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("UV display");
      }
      return;
    }
    case READ_PF_NAV: {
      if (screen == SCREEN_PF) {
        this->sync_press_(BTN_TEMP_UP);  // first press only displays the start hour
        this->sync_goto_(READ_PF_SHOW);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("PF menu navigation");
      }
      return;
    }
    case READ_PF_SHOW:
    case READ_PF_CYCLES:
    case READ_PF_DURATION: {
      // Jets 1 confirms the value shown (unchanged) and moves to the next PF screen; the
      // passive observer publishes each confirmed value.
      int ok1 = SCREEN_PF_START, ok2 = SCREEN_PF_START_EDIT, next = READ_PF_CYCLES;
      if (this->sync_step_ == READ_PF_CYCLES) {
        ok1 = SCREEN_PF_CYCLES;
        ok2 = SCREEN_PF_CYCLES_EDIT;
        next = READ_PF_DURATION;
      } else if (this->sync_step_ == READ_PF_DURATION) {
        ok1 = ok2 = SCREEN_PF_DURATION;
        next = READ_PF_SAVE;
      }
      if (screen == ok1 || screen == ok2) {
        if (value != 0 && now - this->sync_step_ms_ > READ_DWELL_MS && now - this->sync_press_ms_ > READ_DWELL_MS) {
          this->sync_press_(BTN_P1);
          this->sync_goto_(next);
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS + READ_DWELL_MS) {
        this->abort_clock_sync_("PF screens");
      }
      return;
    }
    case READ_PF_SAVE: {
      if (screen == SCREEN_PF) {
        ESP_LOGI(TAG, "Primary Filter settings read");
        this->sync_goto_(TASK_WAIT_MAIN);
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("PF confirm");
      }
      return;
    }
    case READ_LANG_NAV: {
      const int expect = NAV_TO_LANG[this->sync_nav_index_];
      if (screen == expect) {
        if (expect == SCREEN_LANG) {
          this->sync_press_(BTN_TEMP_UP);  // first press only displays the current language
          this->sync_goto_(READ_LANG_SHOW);
        } else {
          this->sync_nav_index_++;
          this->sync_press_(BTN_MENU);
          this->sync_step_ms_ = now;
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("LANG menu navigation");
      }
      return;
    }
    case READ_LANG_SHOW: {
      // English is 0, so wait for f15 to settle instead of waiting for a non-zero value.
      if (screen == SCREEN_LANG_SHOW) {
        if (now - this->sync_step_ms_ > READ_DWELL_MS) {
          // f15 here is always 0: the language is known only from a confirmed change.
          ESP_LOGI(TAG, "Language display screen (f15 %d, no value)", value);
          this->sync_goto_(TASK_WAIT_MAIN);  // no further presses: the menu times out unsaved
        }
      } else if (now - this->sync_step_ms_ > SYNC_STEP_TIMEOUT_MS) {
        this->abort_clock_sync_("LANG display");
      }
      return;
    }
    case TASK_WAIT_MAIN: {
      if ((screen == SCREEN_MAIN)) {
        // Back on the main screen after a write: settings with a status field are checked there.
        bool write_ok = this->write_verify_kind_ != WRITE_NONE;
        if (this->write_verify_kind_ == WRITE_HEAT || this->write_verify_kind_ == WRITE_TEMP_UNIT) {
          bool ok;
          if (this->write_verify_kind_ == WRITE_HEAT) {
            const char *cur = heater_mode_name_(this->prior_status_[6]);
            const int v = this->write_verify_value_;
            ok = cur != nullptr && std::string(cur) == (v == 0 ? "Auto" : v == 1 ? "Eco" : "Day");
          } else {
            ok = this->spa_fahrenheit_ == (this->write_verify_value_ == 1);
          }
          if (!ok) {
            write_ok = false;
            this->decoder_notice_(std::string(write_name_(this->write_verify_kind_)) +
                                  " was saved but the spa does not show it");
          }
        }
        // A write that worked clears the notice of an earlier failed write (other notices keep their hour).
        if (write_ok && this->decoder_notice_text_sensor_ != nullptr &&
            this->decoder_notice_text_sensor_->state.find(" not changed: ") != std::string::npos) {
          this->cancel_timeout("decoder_notice_clear");
          this->decoder_notice_text_sensor_->publish_state("OK");
        }
        this->write_verify_kind_ = WRITE_NONE;
        this->sync_step_ = SYNC_IDLE;
        this->start_next_task_();
      } else if (now - this->sync_step_ms_ > 60000) {
        this->abort_clock_sync_("menu did not return to main screen");
      }
      return;
    }
    default:
      this->sync_step_ = SYNC_IDLE;
      return;
  }
}

static const uint32_t DEBOUNCE_MS = 2000;

void SundanceSpa::debounce_(DebounceText &d, const std::string &v) {
  if (d.sensor == nullptr)
    return;
  if (!d.sensor->has_state()) {  // first value after boot: no reason to wait
    d.sensor->publish_state(v);
    d.pending = false;
  } else if (v == d.sensor->state) {
    d.pending = false;  // back to the published value before it was confirmed: a blip
  } else if (!d.pending || v != d.want) {
    d.want = v;
    d.since = millis();
    d.pending = true;
  }
}

void SundanceSpa::debounce_(DebounceBin &d, bool v) {
  if (d.sensor == nullptr)
    return;
  if (!d.sensor->has_state()) {
    d.sensor->publish_state(v);
    d.pending = false;
  } else if (v == d.sensor->state) {
    d.pending = false;
  } else if (!d.pending || v != d.want) {
    d.want = v;
    d.since = millis();
    d.pending = true;
  }
}

void SundanceSpa::flush_debounced_() {
  const uint32_t now = millis();
  for (DebounceText *d : {&this->circ_status_db_, &this->clearray_db_}) {
    if (d->pending && now - d->since >= DEBOUNCE_MS) {
      d->pending = false;
      d->sensor->publish_state(d->want);
    }
  }
  for (DebounceBin *d : {&this->heater_db_, &this->pumps_running_db_, &this->circ_auto_db_, &this->pump1_running_db_,
                         &this->pump2_running_db_}) {
    if (d->pending && now - d->since >= DEBOUNCE_MS) {
      d->pending = false;
      d->sensor->publish_state(d->want);
    }
  }
}

// Run time counters (count up from the moment each item turns on, reset when it turns off).
// The spa switches manual jets off after exactly 20 min and lights after 2 h, timed from activation
// (2026-10-24); the counters only show elapsed time and never act on it.
void SundanceSpa::service_run_timers_() {
  const uint32_t now = millis();
  if (now - this->run_timer_tick_ms_ < 1000 || this->prior_status_.size() < 16)
    return;
  this->run_timer_tick_ms_ = now;
  const uint8_t f1 = this->prior_status_[1], f2 = this->prior_status_[2];
  const bool jet1_high = (f2 & 0x10) != 0, jet2 = (f1 & 0x04) != 0;
  const bool low = (f1 & 0x40) != 0 && !jet1_high;
  // Item order = ACTIVITY_* in sundance_spa.h.
  //  jet1: by the Jets button, high (f2 bit4) or low without the circulation bits (f1 = bit6 only)
  //  circulation: any continuous low-speed run (filtration, heating, manual)
  //  spa_in_use: the owner's definition of "in the spa" — Jet 2 on or Jet 1 high
  //  heating_run: the pump runs only for heat demand (same rule as Primary Filter "Circulation (Heating)")
  //  filter_cycle: scheduled filtration flag (f10 bit7); stays set when a jet interrupts the cycle
  const bool on[ACTIVITY_COUNT] = {
      jet1_high || (f1 & 0xE0) == 0x40,
      jet2,
      this->light_on_,
      low,
      this->heater_on_,
      jet2 || jet1_high,
      low && (f1 & 0x20) != 0 && (f1 & 0x80) == 0 && !this->filter_cycle_ && !jet2,
      this->filter_cycle_,
  };
  sensor::Sensor *const sens[ACTIVITY_COUNT] = {this->jet1_run_sensor_, this->jet2_run_sensor_,
                                                this->light_run_sensor_, this->circ_run_sensor_,
                                                this->heater_run_sensor_, nullptr, nullptr, nullptr};
  if (!this->act_init_) {  // first tick: take the states as they are
    this->act_init_ = true;
    for (int i = 0; i < ACTIVITY_COUNT; i++) {
      this->act_on_[i] = on[i];
      this->run_since_ms_[i] = on[i] ? now : 0;
    }
  }
  for (int i = 0; i < ACTIVITY_COUNT; i++) {
    // A change counts once it has held DEBOUNCE_MS (3 ticks); the run time then starts from when the
    // change began, so totals are not shortened.
    if (on[i] == this->act_on_[i]) {
      this->act_change_ms_[i] = 0;
    } else if (this->act_change_ms_[i] == 0) {
      this->act_change_ms_[i] = now;
    } else if (now - this->act_change_ms_[i] >= DEBOUNCE_MS) {
      this->act_on_[i] = on[i];
      this->run_since_ms_[i] = on[i] ? this->act_change_ms_[i] : 0;
      this->act_change_ms_[i] = 0;
    }
    const bool is_on = this->act_on_[i];
    const uint32_t ran = is_on ? now - this->run_since_ms_[i] : 0;
    if (sens[i] != nullptr) {
      const float minutes = static_cast<float>(ran / 60000UL);
      if (!sens[i]->has_state() || sens[i]->state != minutes)
        sens[i]->publish_state(minutes);
    }
    // Active = the raw state (for total time); Session = on only once the item has run for at least
    // the configured minimum (for counting real uses, not short presses).
    const auto &a = this->activity_[i];
    if (a.active != nullptr && (!a.active->has_state() || a.active->state != is_on))
      a.active->publish_state(is_on);
    const bool session = is_on && ran >= a.session_min_ms;
    if (a.session != nullptr && (!a.session->has_state() || a.session->state != session))
      a.session->publish_state(session);
  }
}

// Called from loop(): automatic sync after a power cut (spa clock reset) or drift, rate-limited.
void SundanceSpa::loop_680_() {
  const uint32_t now = millis();
  this->flush_debounced_();
  this->service_run_timers_();
  // Light/clock state machines also need ticks between frames.
  this->service_light680_();
  if (this->clock_sync_active_())
    return;
  if (!this->clock_auto_sync_ || this->time_ == nullptr || now - this->last_auto_check_ms_ < 30000)
    return;
  this->last_auto_check_ms_ = now;
  auto t = this->time_->now();
  if (!t.is_valid() || this->prior_status_.size() < 16 || this->last_status_ms_ == 0 ||
      now - this->last_status_ms_ > 5000 || this->screen_() != SCREEN_MAIN) {
    ESP_LOGD(TAG, "Auto check skipped (time valid %d, screen %d)", t.is_valid() ? 1 : 0,
             this->prior_status_.size() >= 16 ? this->screen_() : -1);
    return;
  }
  // Nightly routine at 00:05 (user's choice: clock sync if needed, then ClearRay Bulb and
  // Primary Filter reads). Retried until 00:30 if the spa is busy.
  if (t.hour == 0 && t.minute >= 5 && t.minute < 30 && this->last_routine_yday_ != t.day_of_year) {
    this->last_routine_yday_ = t.day_of_year;
    ESP_LOGI(TAG, "Nightly menu routine");
    this->request_settings_read(true);
    return;
  }
  // After every boot (incl. a power cut, which also powers the ESP down) the menu-only values are
  // unknown: read them once ~2 min after start, with a clock sync first if the spa clock is off.
  if (!this->boot_read_done_ && now > 120000) {
    this->boot_read_done_ = true;
    ESP_LOGI(TAG, "Boot menu routine");
    this->request_settings_read(true);
    return;
  }
  if ((t.hour == 23 && t.minute >= 55) || (t.hour == 0 && t.minute <= 5))
    return;  // avoid racing midnight
  const bool off = this->clock_needs_sync_();
  this->sync_mismatch_count_ = off ? this->sync_mismatch_count_ + 1 : 0;
  if (this->sync_mismatch_count_ >= 2 &&
      (this->last_sync_attempt_ms_ == 0 || now - this->last_sync_attempt_ms_ > 30UL * 60UL * 1000UL)) {
    ESP_LOGI(TAG, "Spa clock/date off — starting automatic sync");
    this->sync_mismatch_count_ = 0;
    this->request_clock_sync(false);
  }
}

}  // namespace sundance_spa
}  // namespace esphome
