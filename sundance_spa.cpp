#include "sundance_spa.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace sundance_spa {

static const char *const TAG = "sundance_spa";

uint8_t SundanceSpa::sundance_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0xB5;
  for (size_t cur = 0; cur < len; cur++) {
    for (int i = 0; i < 8; i++) {
      uint8_t bit = crc & 0x80;
      crc = static_cast<uint8_t>(((crc << 1) & 0xFF) | ((data[cur] >> (7 - i)) & 0x01));
      if (bit) {
        crc = static_cast<uint8_t>(crc ^ 0x07);
      }
    }
  }
  for (int i = 0; i < 8; i++) {
    uint8_t bit = crc & 0x80;
    crc = static_cast<uint8_t>((crc << 1) & 0xFF);
    if (bit) {
      crc = static_cast<uint8_t>(crc ^ 0x07);
    }
  }
  return static_cast<uint8_t>(crc ^ 0x02);
}

std::vector<uint8_t> SundanceSpa::xormsg(const uint8_t *data, size_t len) {
  std::vector<uint8_t> out;
  out.reserve(len / 2);
  for (size_t i = 0; i + 1 < len; i += 2) {
    out.push_back(static_cast<uint8_t>(data[i] ^ data[i + 1] ^ 0x01));
  }
  return out;
}

bool SundanceSpa::channel_in_(const std::vector<uint8_t> &list, uint8_t ch) const {
  return std::find(list.begin(), list.end(), ch) != list.end();
}

float SundanceSpa::get_min_temperature() const {
  // 680 manual: 65-104 F / 18-40 C
  return this->temp_scale_ == TempScale::CELSIUS ? 18.0f : 65.0f;
}
float SundanceSpa::get_max_temperature() const {
  return this->temp_scale_ == TempScale::CELSIUS ? 40.0f : 104.0f;
}
float SundanceSpa::get_temperature_step() const {
  return this->temp_scale_ == TempScale::CELSIUS ? 0.5f : 1.0f;
}

float SundanceSpa::native_to_display_temp_(int raw) const {
  if (raw < 0 || raw == 255) {
    return NAN;
  }
  // The spa reports °C in half degrees and °F in whole degrees (panel TEMP menu, status f14 bit6).
  if (this->spa_fahrenheit_) {
    if (this->temp_scale_ == TempScale::CELSIUS)
      return std::round((raw - 32) * 10.0f / 9.0f) / 2.0f;  // nearest 0.5 °C
    return static_cast<float>(raw);
  }
  if (this->temp_scale_ == TempScale::CELSIUS) {
    return raw / 2.0f;
  }
  return raw * 0.9f + 32.0f;
}

int SundanceSpa::display_to_native_temp_(float temp) const {
  if (this->spa_fahrenheit_) {
    if (this->temp_scale_ == TempScale::CELSIUS)
      return static_cast<int>(std::lround(temp * 9.0f / 5.0f + 32.0f));
    return static_cast<int>(std::lround(temp));
  }
  if (this->temp_scale_ == TempScale::CELSIUS) {
    return static_cast<int>(std::lround(temp * 2.0f));
  }
  return static_cast<int>(std::lround((temp - 32.0f) / 0.9f));
}

LightLevel SundanceSpa::brightness_to_level(int brightness) {
  if (brightness <= 0)
    return LightLevel::LEVEL_OFF;
  if (brightness >= 70)
    return LightLevel::LEVEL_HIGH;
  if (brightness >= 35)
    return LightLevel::LEVEL_MEDIUM;
  return LightLevel::LEVEL_LOW;
}

const char *SundanceSpa::light_level_name(LightLevel level) {
  switch (level) {
    case LightLevel::LEVEL_OFF:
      return "Off";
    case LightLevel::LEVEL_LOW:
      return "Low";
    case LightLevel::LEVEL_MEDIUM:
      return "Medium";
    case LightLevel::LEVEL_HIGH:
      return "High";
  }
  return "Off";
}

const char *SundanceSpa::light_mode_name(int mode) {
  // Mode IDs from HyperActiveJ/sundanceRS485 LIGHT_MODE_MAP
  switch (mode) {
    case 0:
      return "Off";
    case 1:
      return "White";
    case 2:
      return "Blue";
    case 3:
      return "Green";
    case 6:
      return "Red";
    case 7:
      return "Violet";
    case 8:
      return "Amber";
    case 9:
      return "Aqua";
    case 127:
      return "Slow Blend";
    case 128:
      return "Fast Blend";
    case 255:
      return "Frozen Blend";
    default:
      return "Unknown";
  }
}

int SundanceSpa::light_mode_id(const std::string &name) {
  if (name == "White")
    return 1;
  if (name == "Blue")
    return 2;
  if (name == "Green")
    return 3;
  if (name == "Red")
    return 6;
  if (name == "Violet")
    return 7;
  if (name == "Amber")
    return 8;
  if (name == "Aqua")
    return 9;
  if (name == "Slow Blend")
    return 127;
  if (name == "Fast Blend")
    return 128;
  if (name == "Frozen Blend")
    return 255;
  if (name == "Off")
    return 0;
  return NO_CHANGE_REQUESTED;
}

const char *SundanceSpa::circ_status_name_(bool running, bool manual) {
  // 680 "Primary Filter": only the scheduled filtration (pump 1 low + ClearRay). Manual low
  // is the separate "Circulation" switch and does not run the UV lamp.
  if (running && !manual)
    return "Running";
  return "Off";
}

void SundanceSpa::register_sensor_callback(uint8_t datapoint, const std::function<void(float)> &func) {
  switch (datapoint) {
    case DP_CURRENT_TEMP:
      this->current_temp_cbs_.push_back(func);
      break;
    case DP_TARGET_TEMP:
      this->target_temp_cbs_.push_back(func);
      break;
    default:
      ESP_LOGW(TAG, "Unknown sensor datapoint %u", datapoint);
      break;
  }
}

void SundanceSpa::register_binary_sensor_callback(uint8_t datapoint, const std::function<void(bool)> &func) {
  switch (datapoint) {
    case DP_HEATER:
      this->heater_cbs_.push_back(func);
      break;
    case DP_CIRC:
      this->circ_cbs_.push_back(func);
      break;
    case DP_REST:
      this->rest_cbs_.push_back(func);
      break;
    case DP_JET1:
      this->jet1_cbs_.push_back(func);
      break;
    case DP_JET2:
      this->jet2_cbs_.push_back(func);
      break;
    default:
      ESP_LOGW(TAG, "Unknown binary sensor datapoint %u", datapoint);
      break;
  }
}

void SundanceSpa::setup() {
  this->rx_window_.clear();
  this->rx_window_.reserve(RX_WINDOW_MAX);
  this->reset_channel_state_();
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->setup();
    this->flow_control_pin_->digital_write(false);
  }
  this->check_uart_settings(115200, 1, uart::UART_CONFIG_PARITY_NONE, 8);
  this->publish_light_status_();
  this->circ_status_db_.sensor = this->circ_status_text_sensor_;
  this->clearray_db_.sensor = this->clearray_text_sensor_;
  this->heater_db_.sensor = this->heater_binary_sensor_;
  this->pumps_running_db_.sensor = this->pumps_running_sensor_;
  this->circ_auto_db_.sensor = this->circ_auto_sensor_;
  // Known states from boot instead of "unknown" until the first press / sync.
  if (this->panel_button_sensor_ != nullptr)
    this->panel_button_sensor_->publish_state("Idle");
  if (this->clock_sync_text_sensor_ != nullptr)
    this->clock_sync_text_sensor_->publish_state("Not synced yet");
  if (this->decoder_notice_text_sensor_ != nullptr)
    this->decoder_notice_text_sensor_->publish_state("OK");
  // Settings / Service Lock survive an ESP restart (they cannot be read back from the bus).
  this->lock_pref_ = global_preferences->make_preference<uint8_t>(0x5D10C4A1UL);
  uint8_t locks = 0;
  if (this->lock_pref_.load(&locks)) {
    this->sl_on_ = (locks & 1) != 0;
    this->svl_on_ = (locks & 2) != 0;
  }
  // Language: f15 never shows the current one, so the last confirmed change is kept across restarts.
  this->lang_pref_ = global_preferences->make_preference<uint8_t>(0x5D10C4A2UL);
  uint8_t lang = 255;
  if (this->lang_pref_.load(&lang) && lang < 4)
    this->publish_language_(lang, false);
}

static std::string diag_hex_(const uint8_t *data, size_t len) {
  std::string out;
  char buf[4];
  for (size_t i = 0; i < len; i++) {
    snprintf(buf, sizeof(buf), "%02X", data[i]);
    if (i > 0)
      out += ' ';
    out += buf;
  }
  return out;
}

// Logs the full decoded payload plus every byte that changed, with its index and bit diff,
// so a topside button press can be mapped to exact byte/bit positions.
void SundanceSpa::log_decoded_diff_(const char *label, const std::vector<uint8_t> &prev,
                                    const std::vector<uint8_t> &cur) {
  if (!this->diag_enabled_)
    return;
  ESP_LOGI(TAG, "[DIAG] %s (%u bytes): %s", label, static_cast<unsigned>(cur.size()),
           diag_hex_(cur.data(), cur.size()).c_str());
  if (prev.size() != cur.size())
    return;
  for (size_t i = 0; i < cur.size(); i++) {
    if (prev[i] == cur[i])
      continue;
    const uint8_t x = prev[i] ^ cur[i];
    char bits[9];
    for (int b = 7; b >= 0; b--)
      bits[7 - b] = (x >> b) & 1 ? '^' : '.';
    bits[8] = '\0';
    ESP_LOGI(TAG, "[DIAG]   %s[%2u]: %02X -> %02X  (%3u -> %3u)  changed bits 76543210=%s", label,
             static_cast<unsigned>(i), prev[i], cur[i], prev[i], cur[i], bits);
  }
}

void SundanceSpa::report_bus_stats_() {
  if (!this->diag_enabled_)
    return;
  if (millis() - this->diag_last_report_ms_ < 30000)
    return;
  this->diag_last_report_ms_ = millis();
  ESP_LOGI(TAG, "[DIAG] bus: rx_bytes=%u frames_ok=%u bad_crc=%u channel=%d", this->diag_rx_bytes_,
           this->rx_frame_ok_, this->diag_bad_crc_, this->channel_);
  // Periodic full dump (decimal, per field) so slow counters can be matched against panel menus.
  for (int which = 0; which < 2; which++) {
    const std::vector<uint8_t> &d = which == 0 ? this->prior_status_ : this->prior_lights_;
    if (d.empty())
      continue;
    std::string out;
    char buf[16];
    for (size_t i = 0; i < d.size(); i++) {
      snprintf(buf, sizeof(buf), "%sf%u=%u", i ? " " : "", static_cast<unsigned>(i), d[i]);
      out += buf;
    }
    ESP_LOGI(TAG, "[DIAG] %s dec: %s", which == 0 ? "status" : "lights", out.c_str());
  }
  if (this->rx_frame_ok_ == 0 && !this->diag_last_bad_frame_.empty()) {
    ESP_LOGI(TAG, "[DIAG] last rejected frame: %s",
             diag_hex_(this->diag_last_bad_frame_.data(), this->diag_last_bad_frame_.size()).c_str());
  }
  if (this->rx_frame_ok_ == 0 && !this->rx_window_.empty()) {
    const size_t n = std::min<size_t>(this->rx_window_.size(), 48);
    ESP_LOGI(TAG, "[DIAG] raw rx window: %s", diag_hex_(this->rx_window_.data(), n).c_str());
  }
}

void SundanceSpa::dump_config() {
  ESP_LOGCONFIG(TAG, "Sundance Spa:");
  ESP_LOGCONFIG(TAG, "  Temperature scale: %s", this->temp_scale_ == TempScale::CELSIUS ? "C" : "F");
  ESP_LOGCONFIG(TAG, "  RX invert: %s", YESNO(this->rx_invert_));
  LOG_PIN("  Flow Control Pin: ", this->flow_control_pin_);
  if (this->lights_select_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Lights select: yes");
  if (this->light_color_select_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Light Color select: yes");
  if (this->light_status_sensor_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Light Status: yes");
}

void SundanceSpa::reset_channel_state_() {
  this->channel_ = -1;
  this->detect_channel_state_ = 0;
  this->discovered_channels_.clear();
  this->active_channels_.clear();
  this->tx_queue_.clear();
}

void SundanceSpa::set_tx_enable_(bool enable) {
  if (this->flow_control_pin_ != nullptr) {
    this->flow_control_pin_->digital_write(enable);
  }
}

void SundanceSpa::send_raw_(const std::vector<uint8_t> &frame) {
  if (frame.empty()) {
    return;
  }
  this->set_tx_enable_(true);
  if (this->flow_control_pin_ != nullptr) {
    delayMicroseconds(50);
  }
  if (this->rx_invert_) {
    std::vector<uint8_t> inv(frame.size());
    for (size_t i = 0; i < frame.size(); i++) {
      inv[i] = static_cast<uint8_t>(~frame[i]);
    }
    this->write_array(inv.data(), inv.size());
  } else {
    this->write_array(frame.data(), frame.size());
  }
  this->flush();
  if (this->flow_control_pin_ != nullptr) {
    delayMicroseconds(50);
  }
  this->set_tx_enable_(false);
}

bool SundanceSpa::light_seek_active_() const {
  return this->wanted_light_level_ != NO_CHANGE_REQUESTED || this->wanted_light_mode_ != NO_CHANGE_REQUESTED ||
         this->wanted_light680_ != NO_CHANGE_REQUESTED;
}

// Queue one button press without the 780 retry/attempt bookkeeping (used by the 680 light
// and clock-sync state machines, which verify every step themselves).
void SundanceSpa::queue_press_(uint8_t button) {
  if (this->channel_ < 0)
    return;
  const uint8_t msg_len = 7;
  std::vector<uint8_t> frame(9);
  frame[0] = M_STARTEND;
  frame[1] = msg_len;
  frame[2] = static_cast<uint8_t>(this->channel_);
  frame[3] = 0xBF;
  frame[4] = MSG_CC_REQ;
  frame[5] = button;
  frame[6] = 0x00;
  frame[7] = sundance_crc8(&frame[1], msg_len - 1);
  frame[8] = M_STARTEND;
  this->tx_queue_.push_back(frame);
  this->last_own_press_ms_ = millis();
  if (button == BTN_CLEAR_RAY)
    this->last_circ_press_ms_ = this->last_own_press_ms_;
  ESP_LOGD(TAG, "Queued press %u", button);
}

void SundanceSpa::publish_light_status_() {
  if (this->light_status_sensor_ == nullptr) {
    return;
  }
  const char *status = "Off";
  if (this->light_seek_active_()) {
    status = "Seeking";
  } else if (this->light_on_ || this->light_brightness_ > 0) {
    status = "On";
  }
  this->light_status_sensor_->publish_state(status);
}

void SundanceSpa::clear_pending_commands_() {
  this->wanted_temp_ = NO_CHANGE_REQUESTED;
  for (int &p : this->wanted_pump_) {
    p = NO_CHANGE_REQUESTED;
  }
  this->wanted_light_level_ = NO_CHANGE_REQUESTED;
  this->wanted_light_mode_ = NO_CHANGE_REQUESTED;
  this->wanted_jet1_ = NO_CHANGE_REQUESTED;
  this->command_attempts_ = 0;
  this->check_counter_ = 0;
  this->light_check_counter_ = 0;
}

void SundanceSpa::queue_cc_button_(uint8_t button) {
  if (this->channel_ < 0) {
    ESP_LOGW(TAG, "Cannot queue button 0x%02X — no channel yet", button);
    return;
  }
  if (this->command_attempts_ > MAX_COMMAND_ATTEMPTS) {
    this->decoder_notice_("Command not confirmed by the spa after " + std::to_string(this->command_attempts_) +
                          " attempts");
    this->clear_pending_commands_();
    this->publish_states_();
    return;
  }

  const uint8_t msg_len = 7;
  std::vector<uint8_t> frame(9);
  frame[0] = M_STARTEND;
  frame[1] = msg_len;
  frame[2] = static_cast<uint8_t>(this->channel_);
  frame[3] = 0xBF;
  frame[4] = MSG_CC_REQ;
  frame[5] = button;
  frame[6] = 0x00;
  frame[7] = sundance_crc8(&frame[1], msg_len - 1);
  frame[8] = M_STARTEND;

  this->tx_queue_.push_back(frame);
  this->last_own_press_ms_ = millis();
  if (button == BTN_CLEAR_RAY)
    this->last_circ_press_ms_ = this->last_own_press_ms_;
  this->command_attempts_++;
  ESP_LOGD(TAG, "Queued button %u (q=%u)", button, static_cast<unsigned>(this->tx_queue_.size()));
}

void SundanceSpa::send_channel_assignment_request_() {
  const uint8_t msg_len = 8;
  std::vector<uint8_t> frame(10);
  frame[0] = M_STARTEND;
  frame[1] = msg_len;
  frame[2] = 0xFE;
  frame[3] = 0xBF;
  frame[4] = MSG_CHANNEL_ASSIGNMENT_REQ;
  frame[5] = 0x02;
  frame[6] = 0xF1;
  frame[7] = 0x73;
  frame[8] = sundance_crc8(&frame[1], msg_len - 1);
  frame[9] = M_STARTEND;
  this->last_channel_request_ms_ = millis();
  this->send_raw_(frame);
  ESP_LOGD(TAG, "Channel assignment request sent");
}

void SundanceSpa::send_channel_assignment_ack_() {
  if (this->channel_ < 0) {
    return;
  }
  const uint8_t msg_len = 5;
  std::vector<uint8_t> frame(7);
  frame[0] = M_STARTEND;
  frame[1] = msg_len;
  frame[2] = static_cast<uint8_t>(this->channel_);
  frame[3] = 0xBF;
  frame[4] = MSG_CHANNEL_ASSIGNMENT_ACK;
  frame[5] = sundance_crc8(&frame[1], msg_len - 1);
  frame[6] = M_STARTEND;
  this->send_raw_(frame);
}

void SundanceSpa::send_existing_client_response_() {
  if (this->channel_ < 0) {
    return;
  }
  const uint8_t msg_len = 8;
  std::vector<uint8_t> frame(10);
  frame[0] = M_STARTEND;
  frame[1] = msg_len;
  frame[2] = static_cast<uint8_t>(this->channel_);
  frame[3] = 0xBF;
  frame[4] = MSG_EXISTING_CLIENT_RESPONSE;
  frame[5] = 0x04;
  frame[6] = 0x08;
  frame[7] = 0x00;
  frame[8] = sundance_crc8(&frame[1], msg_len - 1);
  frame[9] = M_STARTEND;
  this->send_raw_(frame);
}

void SundanceSpa::on_set_temp(float temp) {
  const float tmin = this->get_min_temperature();
  const float tmax = this->get_max_temperature();
  if (std::isnan(temp) || temp < tmin - 0.25f || temp > tmax + 0.25f) {
    ESP_LOGW(TAG, "Set temperature %.1f out of range [%.1f, %.1f]", temp, tmin, tmax);
    return;
  }
  this->wanted_temp_ = this->display_to_native_temp_(temp);
  this->command_attempts_ = 0;
  this->check_counter_ = 0;
  ESP_LOGI(TAG, "Target temperature %.1f requested (native=%d, current set=%.1f)", temp, this->wanted_temp_,
           this->set_temp_);
  this->service_command_retries_();
}

void SundanceSpa::set_light_level(const std::string &level_name) {
  LightLevel level = LightLevel::LEVEL_OFF;
  if (level_name == "Off") {
    level = LightLevel::LEVEL_OFF;
  } else if (level_name == "Low") {
    level = LightLevel::LEVEL_LOW;
  } else if (level_name == "Medium") {
    level = LightLevel::LEVEL_MEDIUM;
  } else if (level_name == "High") {
    level = LightLevel::LEVEL_HIGH;
  } else {
    ESP_LOGW(TAG, "Unknown light level '%s'", level_name.c_str());
    return;
  }

  const LightLevel current = brightness_to_level(this->light_brightness_);
  if (current == level && this->wanted_light_level_ == NO_CHANGE_REQUESTED) {
    ESP_LOGD(TAG, "Lights already %s", level_name.c_str());
    return;
  }

  this->wanted_light_level_ = static_cast<int>(level);
  this->command_attempts_ = 0;
  this->light_check_counter_ = 0;
  ESP_LOGI(TAG, "Lights set to %s (current brightness=%d)", level_name.c_str(), this->light_brightness_);
  // Hold the requested option in HA while cycling; skip intermediate states.
  this->publish_light_status_();
  this->publish_light_selects_();
  this->service_light_retries_();
}

void SundanceSpa::set_light_color(const std::string &color_name) {
  // 680: one Light button and one select ("Light") with Off + the 10 manual modes.
  this->set_light_680(color_name);
  return;
  const int mode = light_mode_id(color_name);
  if (mode == NO_CHANGE_REQUESTED) {
    ESP_LOGW(TAG, "Unknown light color '%s'", color_name.c_str());
    return;
  }
  // Color cannot be changed while lights are off (same as HyperActiveJ).
  if (this->light_brightness_ <= 0) {
    ESP_LOGW(TAG, "Cannot set light color while lights are Off — set Lights first");
    this->publish_light_selects_();
    return;
  }
  if (this->light_mode_ == mode && this->wanted_light_mode_ == NO_CHANGE_REQUESTED) {
    ESP_LOGD(TAG, "Light color already %s", color_name.c_str());
    return;
  }

  this->wanted_light_mode_ = mode;
  this->command_attempts_ = 0;
  this->light_check_counter_ = 0;
  ESP_LOGI(TAG, "Light color set to %s (current mode=%d)", color_name.c_str(), this->light_mode_);
  // Hold the requested option in HA while cycling; skip intermediate states.
  this->publish_light_status_();
  this->publish_light_selects_();
  this->service_light_retries_();
}

void SundanceSpa::on_toggle_circ() {
  // One ClearRay / circ button press — same as the topside. Do not enter the
  // wanted_pump_ retry loop: during auto filter/heat the spa often will not
  // latch a separate "manual" bit, and retries just thrash the button.
  this->command_attempts_ = 0;
  this->wanted_pump_[2] = NO_CHANGE_REQUESTED;
  this->queue_cc_button_(BTN_CLEAR_RAY);
}

void SundanceSpa::set_pump(uint8_t pump, bool on) {
  if (pump > 2)
    return;
  if (this->channel_ < 0) {
    ESP_LOGW(TAG, "Cannot set pump %u — no channel yet", pump);
    return;
  }
  // Circ is not a reliable on/off target while auto filter/heat may own it.
  // Prefer the Circulation button (single ClearRay press) instead of a switch.
  if (pump == 2) {
    ESP_LOGI(TAG, "Circ set_pump(%s) → single ClearRay press (no state seek)", on ? "ON" : "OFF");
    this->wanted_pump_[2] = NO_CHANGE_REQUESTED;
    this->command_attempts_ = 0;
    this->queue_cc_button_(BTN_CLEAR_RAY);
    return;
  }
  // 680: Jet 1 is one two-speed pump; the "Jet 1" switch is HIGH. Go through the
  // mutually exclusive Jet 1 mode seek (see set_jet1_mode).
  if (pump == 0) {
    this->set_jet1_mode(on ? JET1_HIGH : JET1_OFF);
    return;
  }
  const int desired = on ? 1 : 0;
  if (this->pump_status_[pump] == desired && this->wanted_pump_[pump] == NO_CHANGE_REQUESTED) {
    ESP_LOGD(TAG, "Pump %u already %s", pump, on ? "ON" : "OFF");
    return;
  }
  this->wanted_pump_[pump] = desired;
  this->check_counter_ = 0;
  this->command_attempts_ = 0;
  ESP_LOGI(TAG, "Pump %u set to %s (current=%d)", pump, on ? "ON" : "OFF", this->pump_status_[pump]);
  this->service_command_retries_();
}

// Current Jet 1 state as the 680 reports it: HIGH (pump0 bit), manual LOW ("Circulation",
// manual-circ bit, no UV), or OFF. Automatic filtration (low + UV) counts as OFF here; it is
// reported separately by the Primary Filter sensor.
int SundanceSpa::jet1_mode_() const {
  if (this->pump_status_[0] != 0)
    return JET1_HIGH;
  if (this->manual_circ_ && !this->manual_auto_)
    return JET1_LOW;
  return JET1_OFF;
}

bool SundanceSpa::auto_filtration_() const { return this->circ_on_ && (!this->manual_circ_ || this->manual_auto_); }

void SundanceSpa::set_jet1_mode(int mode) {
  if (this->channel_ < 0) {
    ESP_LOGW(TAG, "Cannot set Jet 1 — no channel yet");
    return;
  }
  if (mode == JET1_LOW && this->auto_filtration_() && this->jet1_mode_() == JET1_OFF) {
    ESP_LOGI(TAG, "Jet 1 LOW requested during automatic filtration — pump already on low, no press");
    this->publish_states_();
    return;
  }
  if (this->jet1_mode_() == mode && this->wanted_jet1_ == NO_CHANGE_REQUESTED) {
    ESP_LOGD(TAG, "Jet 1 already in mode %d", mode);
    return;
  }
  this->wanted_jet1_ = mode;
  this->check_counter_ = 0;
  this->command_attempts_ = 0;
  ESP_LOGI(TAG, "Jet 1 mode %d requested (current=%d, auto filtration=%d)", mode, this->jet1_mode_(),
           this->auto_filtration_() ? 1 : 0);
  this->service_command_retries_();
}

void SundanceSpa::service_command_retries_() {
  if (this->channel_ < 0 || this->check_counter_ > 0 || !this->tx_queue_.empty() || this->menu_in_use_()) {
    return;
  }

  // Jet 1 seek. Panel cycles: Off -> Low -> High -> Off normally, and Filtration -> High ->
  // Filtration during automatic filtration. The Circulation code (BTN_CLEAR_RAY) toggles
  // manual low on/off directly, so Low is entered and left without passing through High.
  if (this->wanted_jet1_ != NO_CHANGE_REQUESTED) {
    const int cur = this->jet1_mode_();
    const int want = this->wanted_jet1_;
    if (cur == want || (want == JET1_LOW && cur == JET1_OFF && this->auto_filtration_())) {
      ESP_LOGI(TAG, "Jet 1 reached mode %d", cur);
      this->wanted_jet1_ = NO_CHANGE_REQUESTED;
      this->command_attempts_ = 0;
    } else {
      uint8_t btn;
      if (want == JET1_HIGH)
        btn = BTN_P1;  // Off->Low->High, Low->High, Filtration->High
      else if (cur == JET1_HIGH)
        btn = BTN_P1;  // High->Off (or back to filtration); Low is then entered via Circulation
      else
        btn = BTN_CLEAR_RAY;  // Off<->Low (manual circulation toggle)
      ESP_LOGD(TAG, "Jet 1 step: mode %d -> want %d, press %u", cur, want, btn);
      this->queue_cc_button_(btn);
      this->check_counter_ = CHECKS_BEFORE_RETRY;
      return;
    }
  }

  if (this->wanted_temp_ != NO_CHANGE_REQUESTED) {
    int current_set_native = NO_CHANGE_REQUESTED;
    if (!std::isnan(this->set_temp_)) {
      current_set_native = this->display_to_native_temp_(this->set_temp_);
    }
    if (current_set_native == this->wanted_temp_) {
      ESP_LOGI(TAG, "Set temperature reached (native=%d)", this->wanted_temp_);
      this->wanted_temp_ = NO_CHANGE_REQUESTED;
      this->command_attempts_ = 0;
    } else if (current_set_native != NO_CHANGE_REQUESTED && this->wanted_temp_ < current_set_native) {
      ESP_LOGD(TAG, "Temp step DOWN (set native %d -> want %d)", current_set_native, this->wanted_temp_);
      this->queue_cc_button_(BTN_TEMP_DOWN);
      this->check_counter_ = CHECKS_BEFORE_RETRY;
      return;
    } else if (current_set_native != NO_CHANGE_REQUESTED) {
      ESP_LOGD(TAG, "Temp step UP (set native %d -> want %d)", current_set_native, this->wanted_temp_);
      this->queue_cc_button_(BTN_TEMP_UP);
      this->check_counter_ = CHECKS_BEFORE_RETRY;
      return;
    }
  }

  for (int i = 0; i < 3; i++) {
    if (this->wanted_pump_[i] == NO_CHANGE_REQUESTED) {
      continue;
    }
    if (this->pump_status_[i] == this->wanted_pump_[i]) {
      ESP_LOGI(TAG, "Pump %d reached desired state %d", i, this->wanted_pump_[i]);
      this->wanted_pump_[i] = NO_CHANGE_REQUESTED;
      this->command_attempts_ = 0;
      continue;
    }
    // Jet pumps only in the seek loop. Circ is single-press (see on_toggle_circ).
    if (i == 2) {
      this->wanted_pump_[i] = NO_CHANGE_REQUESTED;
      continue;
    }
    uint8_t btn = (i == 0) ? BTN_P1 : BTN_P2;
    this->queue_cc_button_(btn);
    this->check_counter_ = CHECKS_BEFORE_RETRY;
    return;
  }
}

void SundanceSpa::service_light_retries_() {
  if (this->channel_ < 0 || this->light_check_counter_ > 0 || !this->tx_queue_.empty()) {
    return;
  }

  if (this->wanted_light_level_ != NO_CHANGE_REQUESTED) {
    const LightLevel current = brightness_to_level(this->light_brightness_);
    if (static_cast<int>(current) == this->wanted_light_level_) {
      ESP_LOGI(TAG, "Lights reached %s", light_level_name(current));
      this->wanted_light_level_ = NO_CHANGE_REQUESTED;
      this->command_attempts_ = 0;
      this->publish_light_selects_();
      this->publish_light_status_();
    } else {
      ESP_LOGD(TAG, "Lights step (brightness=%d level=%s -> want %s)", this->light_brightness_,
               light_level_name(current), light_level_name(static_cast<LightLevel>(this->wanted_light_level_)));
      this->queue_cc_button_(BTN_LIGHT_ON);
      this->light_check_counter_ = CHECKS_BEFORE_RETRY;
      return;
    }
  }

  if (this->wanted_light_mode_ != NO_CHANGE_REQUESTED) {
    if (this->light_brightness_ <= 0) {
      // Wait until lights are on before cycling color.
      return;
    }
    if (this->light_mode_ == this->wanted_light_mode_) {
      ESP_LOGI(TAG, "Light color reached %s", light_mode_name(this->light_mode_));
      this->wanted_light_mode_ = NO_CHANGE_REQUESTED;
      this->command_attempts_ = 0;
      this->publish_light_selects_();
      this->publish_light_status_();
    } else {
      ESP_LOGD(TAG, "Light color step (mode=%d -> want %d)", this->light_mode_, this->wanted_light_mode_);
      this->queue_cc_button_(BTN_LIGHT_COLOR);
      this->light_check_counter_ = CHECKS_BEFORE_RETRY;
      return;
    }
  }
}

void SundanceSpa::publish_light_selects_() {
  if (this->lights_select_ != nullptr) {
    if (this->wanted_light_level_ != NO_CHANGE_REQUESTED) {
      // Sticky target while seeking — no fake "Seeking" option needed.
      this->lights_select_->publish_state(
          light_level_name(static_cast<LightLevel>(this->wanted_light_level_)));
    } else if (this->light_brightness_ >= 0) {
      this->lights_select_->publish_state(light_level_name(brightness_to_level(this->light_brightness_)));
    }
  }
  if (this->light_color_select_ != nullptr) {
    // 680 "Light" select: sticky target while seeking, otherwise the reported mode.
    const int idx = this->wanted_light680_ != NO_CHANGE_REQUESTED ? this->wanted_light680_ : this->light680_index_();
    if (idx != NO_CHANGE_REQUESTED)
      this->light_color_select_->publish_state(light680_name(idx));
    return;
  }
  if (this->light_color_select_ != nullptr) {
    if (this->wanted_light_mode_ != NO_CHANGE_REQUESTED) {
      this->light_color_select_->publish_state(light_mode_name(this->wanted_light_mode_));
    } else if (this->light_mode_ >= 0) {
      const char *name = light_mode_name(this->light_mode_);
      // Don't publish "Off"/"Unknown" into the color select options list.
      if (strcmp(name, "Off") != 0 && strcmp(name, "Unknown") != 0) {
        this->light_color_select_->publish_state(name);
      }
    }
  }
}

void SundanceSpa::publish_states_() {
  if (!std::isnan(this->current_temp_)) {
    for (const auto &cb : this->current_temp_cbs_) {
      if (cb)
        cb(this->current_temp_);
    }
  }
  if (!std::isnan(this->set_temp_)) {
    for (const auto &cb : this->target_temp_cbs_) {
      if (cb)
        cb(this->set_temp_);
    }
  }
  if (this->temp_sensor_ && !std::isnan(this->current_temp_)) {
    this->temp_sensor_->publish_state(this->current_temp_);
  }
  for (const auto &cb : this->heater_cbs_) {
    if (cb)
      cb(this->heater_on_);
  }
  // Circ switch / callbacks track *manual* circ (controllable). Binary sensor below
  // reports whether the pump is physically running (auto or manual).
  for (const auto &cb : this->circ_cbs_) {
    if (cb)
      cb(this->jet1_mode_() == JET1_LOW);  // "Circulation" switch = manual Jet 1 low
  }
  for (const auto &cb : this->rest_cbs_) {
    if (cb)
      cb(this->rest_mode_);
  }
  for (const auto &cb : this->jet1_cbs_) {
    if (cb)
      cb(this->pump_status_[0] != 0);
  }
  for (const auto &cb : this->jet2_cbs_) {
    if (cb)
      cb(this->pump_status_[1] != 0);
  }
  this->debounce_(this->heater_db_, this->heater_on_);
  if (this->circ_binary_sensor_)
    this->circ_binary_sensor_->publish_state(this->circ_on_);
  if (this->circ_status_db_.sensor != nullptr)
    // User-defined states (2026-10-22/26): "Purge" = the 2-minute sequence after power-up or a Service
    // Lock unlock (Jet 1 low, then Jet 2; the controller keeps the manual-circulation bit set for all of
    // it although nobody pressed Circulation — verified 25 Oct outside and 26 Oct inside a filter
    // cycle); "Running" = Jet 1 high or Jet 2 on; "Cleanup" = Jet 2 started by the controller itself
    // (one minute after the first daily filter cycle starts); "Circulation (Manual)" = Jet 1 low started
    // by hand; "Circulation (Auto)" = scheduled filtration; "Circulation (Heating)" = the spa runs the
    // pump for heat demand; otherwise "Off". Purge comes first so presses made meanwhile don't matter.
    this->debounce_(
        this->circ_status_db_,
        this->manual_circ_ && this->manual_auto_                                          ? "Purge"
        : (this->pump_status_[0] != 0 || (this->pump_status_[1] != 0 && !this->jet2_auto_)) ? "Running"
        : this->pump_status_[1] != 0                                                      ? "Cleanup"
        : this->manual_circ_                                                              ? "Circulation (Manual)"
        : !this->circ_on_ || !this->circ_cmd_                                             ? "Off"
        : this->filter_cycle_                                                             ? "Circulation (Auto)"
                                                                                          : "Circulation (Heating)");
  // Any pump running, for any reason (Jet 1 low or high, Jet 2).
  const bool pumps_running = this->circ_on_ || this->pump_status_[0] != 0 || this->pump_status_[1] != 0;
  this->debounce_(this->pumps_running_db_, pumps_running);
  // Each pump on its own: Pump 1 = Jet 1 at either speed (low = circulation, f1 bit6; high = f2 bit4),
  // Pump 2 = Jet 2 (f1 bit2). Pumps Running = at least one of them.
  this->debounce_(this->pump1_running_db_, this->circ_on_ || this->pump_status_[0] != 0);
  this->debounce_(this->pump2_running_db_, this->pump_status_[1] != 0);
  // Circulation the controller runs on its own: scheduled filtration (f10 bit7 stays set while a jet
  // interrupts it), heat demand and the purge — everything except the manual Circulation switch.
  // Only while a pump actually runs: f10 bit7 also stays set while a Service Lock stops the cycle.
  this->debounce_(this->circ_auto_db_, (this->filter_cycle_ && pumps_running) ||
                                         (this->circ_on_ && (!this->manual_circ_ || this->manual_auto_)));
  // ClearRay UV indicator is on whenever the Jet 1 pump runs, at either speed and for any reason
  // (filtration, heating, manual, blow-out; user-observed 2026-10-23).
  this->debounce_(this->clearray_db_, (this->circ_on_ || this->pump_status_[0] != 0) ? "On" : "Off");
  if (this->rest_binary_sensor_)
    this->rest_binary_sensor_->publish_state(this->rest_mode_);
  this->publish_light_selects_();
  this->publish_light_status_();
}

void SundanceSpa::parse_status_update_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 10)
    return;
  const uint8_t *enc = &frame[5];
  const size_t enc_len = frame.size() - 7;
  if (enc_len < 2)
    return;

  std::vector<uint8_t> data = xormsg(enc, enc_len);
  if (enc_len % 2 == 1) {
    this->status_tail_ = enc[enc_len - 1];
    this->publish_raw_tail_();
  }
  if (data.size() < 15) {
    ESP_LOGW(TAG, "Decoded status too short (%u)", static_cast<unsigned>(data.size()));
    return;
  }

  // f14 bit6 is set while the panel is in °F (verified 2026-10-23); f14 bit5 follows Jet 1 high.
  this->spa_fahrenheit_ = ((data[14] >> 6) & 1) != 0;
  if (this->temp_format_text_sensor_ != nullptr) {
    const char *fmt = this->spa_fahrenheit_ ? "°F" : "°C";
    if (!this->temp_format_text_sensor_->has_state() || this->temp_format_text_sensor_->state != fmt)
      this->temp_format_text_sensor_->publish_state(fmt);
  }

  const int time_hour = data[0] ^ 6;
  const int time_minute = data[11];
  const int pump0 = (data[2] >> 4) & 1;
  const int pump1 = (data[1] >> 2) & 1;
  // Bit layout from HyperActiveJ/sundanceRS485 decode notes:
  //   bit6 = circ pump running (auto or manual)
  //   bit7 = manual circ mode (the ClearRay button toggles this)
  const int circ_running = (data[1] >> 6) & 1;
  const int manual_circ = (data[1] >> 7) & 1;
  // f1 bit5: low speed commanded; it drops ~1 s before bit6 when a cycle ends (2026-10-24).
  const bool circ_cmd = ((data[1] >> 5) & 1) != 0;
  const int set_raw = data[8];
  const int cur_raw = data[5] ^ 2;
  const bool heater = ((data[10] >> 6) & 1) != 0;
  // f10 bit7: set during the scheduled filtration cycle, clear when the pump runs for heating (2026-10-23).
  const bool filter_cycle = ((data[10] >> 7) & 1) != 0;
  const int heat_mode = data[6];
  const bool rest = (heat_mode == 34);

  float new_set = this->native_to_display_temp_(set_raw);
  float new_cur = this->native_to_display_temp_(cur_raw);

  bool changed = false;
  // pump_status_[2] tracks *manual* circ — that is what ClearRay can change.
  if (this->pump_status_[0] != pump0 || this->pump_status_[1] != pump1 || this->pump_status_[2] != manual_circ)
    changed = true;
  if (this->circ_on_ != (circ_running != 0) || this->manual_circ_ != (manual_circ != 0))
    changed = true;
  if (pump1 != 0 && this->pump_status_[1] == 0) {
    // No press (topside or ours) in the last 5 s -> the controller started Jet 2 on its own.
    const uint32_t now = millis();
    const uint32_t last = std::max(this->last_panel_press_ms_, this->last_own_press_ms_);
    this->jet2_auto_ = last == 0 || now - last > 5000;
  }
  if (manual_circ != 0 && !this->manual_circ_) {
    // Only a Circulation press (topside or ours) makes it manual; the purge sets the same bit alone.
    const uint32_t now = millis();
    this->manual_auto_ = this->last_circ_press_ms_ == 0 || now - this->last_circ_press_ms_ > 5000;
    if (this->manual_auto_)
      ESP_LOGI(TAG, "Jet 1 low started by the controller (purge) -> automatic circulation");
  } else if (manual_circ == 0) {
    this->manual_auto_ = false;
  }
  if (this->heater_on_ != heater || this->rest_mode_ != rest || this->filter_cycle_ != filter_cycle ||
      this->circ_cmd_ != circ_cmd)
    changed = true;
  if (!std::isnan(new_set) && (std::isnan(this->set_temp_) || std::fabs(this->set_temp_ - new_set) > 0.05f))
    changed = true;
  if (!std::isnan(new_cur) && (std::isnan(this->current_temp_) || std::fabs(this->current_temp_ - new_cur) > 0.05f))
    changed = true;

  this->pump_status_[0] = pump0;
  this->pump_status_[1] = pump1;
  this->pump_status_[2] = manual_circ;
  this->circ_on_ = circ_running != 0;
  this->manual_circ_ = manual_circ != 0;
  this->heater_on_ = heater;
  this->filter_cycle_ = filter_cycle;
  this->circ_cmd_ = circ_cmd;
  this->rest_mode_ = rest;
  if (!std::isnan(new_set))
    this->set_temp_ = new_set;
  if (!std::isnan(new_cur))
    this->current_temp_ = new_cur;
  this->last_status_ms_ = millis();
  if (this->check_counter_ > 0)
    this->check_counter_--;

  bool raw_changed = this->prior_status_.size() != data.size();
  if (!raw_changed) {
    for (size_t i = 0; i < data.size(); i++) {
      // Byte 11 is the clock minute; ignore it so the diff log only shows real state changes.
      if (i != 11 && this->prior_status_[i] != data[i]) {
        raw_changed = true;
        break;
      }
    }
  }
  if (raw_changed)
    this->log_decoded_diff_("status", this->prior_status_, data);
  this->prior_status_ = data;
  this->spa_hour_ = time_hour % 24;
  this->spa_minute_ = (time_minute & 0x7F) % 60;
  this->update_680_status_();

  // 680-series fields, matched against the topside menus (2026-10-22):
  //   f12 = days to water change (plain), f3 = days to filter change XOR 0xFA,
  //   f0 ^ 6 = hour, f11 & 0x7F = minute (bit 7 is an unidentified flag).
  this->publish_counters_();
  if (this->time_text_sensor_ != nullptr) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", time_hour % 24, (time_minute & 0x7F) % 60);
    if (!this->time_text_sensor_->has_state() || this->time_text_sensor_->state != buf)
      this->time_text_sensor_->publish_state(buf);
  }

  if (changed || raw_changed) {
    ESP_LOGD(TAG,
             "Status %02d:%02d set=%.1f cur=%.1f P1=%d P2=%d circ_run=%d circ_man=%d heat=%d rest=%d",
             time_hour, time_minute, this->set_temp_, this->current_temp_, pump0, pump1, circ_running,
             manual_circ, heater ? 1 : 0, rest ? 1 : 0);
    this->publish_states_();
  }
  this->service_command_retries_();
}

// Shows the unread last payload byte of C4 and CA next to the screen key, so the "tail = key XOR constant" idea can
// be checked from the history: C4^key and CA^key should then be the same number every day.
void SundanceSpa::publish_raw_tail_() {
  if (this->raw_tail_text_sensor_ == nullptr)
    return;
  // At most one update per 5 s, in case the byte changes on every frame (it would flood the recorder).
  if (this->raw_tail_ms_ != 0 && millis() - this->raw_tail_ms_ < 5000)
    return;
  char buf[96];
  const int key = this->screen_key_;
  snprintf(buf, sizeof(buf), "C4 %d CA %d key %d C4^key %d CA^key %d", this->status_tail_, this->lights_tail_, key,
           this->status_tail_ >= 0 ? (this->status_tail_ ^ key) : -1,
           this->lights_tail_ >= 0 ? (this->lights_tail_ ^ key) : -1);
  if (this->raw_tail_published_ == buf)
    return;
  this->raw_tail_published_ = buf;
  this->raw_tail_ms_ = millis() | 1;
  this->raw_tail_text_sensor_->publish_state(buf);
}

void SundanceSpa::parse_lights_update_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 10)
    return;
  const uint8_t *enc = &frame[5];
  const size_t enc_len = frame.size() - 7;
  if (enc_len < 2)
    return;
  std::vector<uint8_t> data = xormsg(enc, enc_len);
  if (enc_len % 2 == 1) {
    this->lights_tail_ = enc[enc_len - 1];
    this->publish_raw_tail_();
  }
  if (data.size() < 5)
    return;

  if (data != this->prior_lights_) {
    this->log_decoded_diff_("lights", this->prior_lights_, data);
    this->prior_lights_ = data;
    if (this->raw_lights_text_sensor_ != nullptr) {
      std::string s;
      char b[5];
      for (size_t i = 0; i < data.size(); i++) {
        snprintf(b, sizeof(b), i ? " %d" : "%d", data[i]);
        s += b;
      }
      this->raw_lights_text_sensor_->publish_state(s);
    }
  }

  const int brightness = data[1];
  const int mode = data[4];
  // 680: L9 distinguishes High (2) from Low (4) Speed Blend, which share mode 128.
  const int speed = data.size() > 9 ? data[9] : 0;
  const bool speed_changed = speed != this->light_speed_;
  this->light_speed_ = speed;
  // Brightness is authoritative for on/off; some firmwares keep a color mode while dimmed.
  const bool on = brightness > 0;

  const bool changed = (on != this->light_on_) || (brightness != this->light_brightness_) ||
                       (mode != this->light_mode_) || (mode == 128 && speed_changed);
  this->light_brightness_ = brightness;
  this->light_mode_ = mode;
  this->light_on_ = on;

  if (this->light_check_counter_ > 0)
    this->light_check_counter_--;

  if (changed) {
    ESP_LOGD(TAG, "Lights %s brightness=%d mode=%d (%s / %s)", on ? "ON" : "OFF", brightness, mode,
             light_level_name(brightness_to_level(brightness)), light_mode_name(mode));
    this->publish_states_();
  }
  this->service_light_retries_();
  this->service_light680_();
}

// Topside button codes seen on the 680 (b5 ^ b6 of the CC frame).
static std::string panel_button_name_(uint8_t btn) {
  switch (btn) {
    case BTN_IDLE:
      return "Idle";
    case BTN_TEMP_UP:
      return "Warmer";
    case BTN_TEMP_DOWN:
      return "Cooler";
    case BTN_P1:
      return "Jets 1";
    case BTN_P2:
      return "Jets 2";
    case BTN_CLEAR_RAY:
      return "Circulation";
    case BTN_LIGHT_COLOR:
      return "Light";
    case BTN_MENU:
      return "Menu";
    case 209:
      return "Menu (Hold)";
    default: {
      char buf[12];
      snprintf(buf, sizeof(buf), "Code %d", btn);
      return buf;
    }
  }
}

void SundanceSpa::handle_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 7)
    return;

  this->rx_frame_ok_++;

  const uint8_t channel = frame[2];
  const uint8_t mtype = frame[4];

  if (!this->channel_in_(this->diag_seen_types_, mtype)) {
    this->diag_seen_types_.push_back(mtype);
    if (this->msg_types_text_sensor_ != nullptr) {
      std::vector<uint8_t> sorted = this->diag_seen_types_;
      std::sort(sorted.begin(), sorted.end());
      std::string s;
      char b[4];
      for (size_t i = 0; i < sorted.size(); i++) {
        snprintf(b, sizeof(b), i ? " %02X" : "%02X", sorted[i]);
        s += b;
      }
      this->msg_types_text_sensor_->publish_state(s);
    }
    if (this->diag_enabled_)
      ESP_LOGI(TAG, "[DIAG] first msg type 0x%02X (ch 0x%02X, %u bytes): %s", mtype, channel,
             static_cast<unsigned>(frame.size()), diag_hex_(frame.data(), frame.size()).c_str());
  }

  // Decoding aid: payload of every other message type, published when it changes (per type and
  // channel), at most once per 2 s, to look for data outside the status frame (e.g. a jet timer).
  if (this->raw_other_text_sensor_ != nullptr && mtype != MSG_STATUS_UPDATE && mtype != MSG_STATUS_UPDATE_ALT &&
      mtype != MSG_LIGHTS_UPDATE && mtype != MSG_LIGHTS_UPDATE_ALT && mtype != MSG_CC_REQ && mtype != MSG_CC_REQ_ALT) {
    const uint16_t key = static_cast<uint16_t>((mtype << 8) | channel);
    const std::string payload = frame.size() > 7 ? diag_hex_(&frame[5], frame.size() - 7) : std::string("-");
    auto it = this->raw_other_last_.find(key);
    const uint32_t now = millis();
    if ((it == this->raw_other_last_.end() || it->second != payload) && now - this->raw_other_pub_ms_ >= 2000) {
      this->raw_other_last_[key] = payload;
      this->raw_other_pub_ms_ = now;
      char head[16];
      snprintf(head, sizeof(head), "%02X ch%02X: ", mtype, channel);
      this->raw_other_text_sensor_->publish_state(std::string(head) + payload);
    }
  }

  switch (mtype) {
    case MSG_STATUS_UPDATE:
    case MSG_STATUS_UPDATE_ALT:
      this->parse_status_update_(frame);
      break;
    case MSG_LIGHTS_UPDATE:
    case MSG_LIGHTS_UPDATE_ALT:
      this->parse_lights_update_(frame);
      break;
    case MSG_CLIENT_CTS:
      if (this->channel_ < 0) {
        uint32_t now = millis();
        if (now - this->last_channel_request_ms_ > 1000) {
          this->send_channel_assignment_request_();
        }
      }
      break;
    case MSG_CHANNEL_ASSIGNMENT_RESPONSE:
      if (frame.size() > 6) {
        this->channel_ = frame[5];
        ESP_LOGI(TAG, "Assigned channel 0x%02X", this->channel_);
        this->send_channel_assignment_ack_();
      }
      break;
    case MSG_EXISTING_CLIENT_REQ:
      if (this->channel_ >= 0) {
        this->send_existing_client_response_();
      }
      break;
    case MSG_CLEAR_TO_SEND: {
      if (!this->channel_in_(this->discovered_channels_, channel)) {
        this->discovered_channels_.push_back(channel);
        ESP_LOGD(TAG, "CTS channel 0x%02X", channel);
      }
      if (this->channel_ >= 0 && channel == static_cast<uint8_t>(this->channel_)) {
        if (!this->tx_queue_.empty()) {
          auto msg = this->tx_queue_.front();
          this->tx_queue_.pop_front();
          this->send_raw_(msg);
        }
      } else if (this->channel_ < 0) {
        if (this->detect_channel_state_ < DETECT_CHANNEL_NOT_FOUND) {
          this->detect_channel_state_++;
        }
        if (this->detect_channel_state_ >= DETECT_CHANNEL_NOT_FOUND) {
          std::vector<uint8_t> sorted = this->discovered_channels_;
          std::sort(sorted.begin(), sorted.end());
          for (uint8_t ch : sorted) {
            if (!this->channel_in_(this->active_channels_, ch)) {
              this->channel_ = ch;
              ESP_LOGI(TAG, "Claimed free channel 0x%02X", ch);
              break;
            }
          }
        }
      }
      break;
    }
    case MSG_CC_REQ:
    case MSG_CC_REQ_ALT: {
      // Log topside button presses so the 680's button codes (light, menu, arrows) can be mapped
      // from real presses. Payload is XOR-folded like status, so the logged value is b5 ^ b6 ^ 1 and the real
      // button code is b5 ^ b6 (same as Pete's 780 codes; see btn below). The panel sends
      // this frame continuously with an idle code, so only log when a channel's code changes.
      if (this->channel_ < 0 || channel != static_cast<uint8_t>(this->channel_)) {
        static int16_t last_code[256];
        static bool last_code_init = false;
        if (!last_code_init) {
          for (auto &c : last_code)
            c = -1;
          last_code_init = true;
        }
        if (frame.size() >= 9) {
          const int16_t code = static_cast<int16_t>(frame[5] ^ frame[6] ^ 1);
          if (code != last_code[channel]) {
            if (this->diag_enabled_)
              ESP_LOGI(TAG, "[DIAG] panel press ch=0x%02X code %d -> %d (raw %02X %02X)", channel, last_code[channel],
                     code, frame[5], frame[6]);
            // Real button code is b5^b6 (the logged value is XOR 1); idle = BTN_IDLE.
            const uint8_t btn = static_cast<uint8_t>(code ^ 1);
            // A press lasts ~0.1 s, shorter than the API's state batching, so the idle code would
            // overwrite it before Home Assistant sees it: hold each press for 1 s before showing idle.
            if (this->panel_button_sensor_ != nullptr && btn != BTN_IDLE) {
              this->panel_button_sensor_->publish_state(panel_button_name_(btn));
              this->set_timeout("panel_btn_idle", 1000,
                                [this]() { this->panel_button_sensor_->publish_state("Idle"); });
            }
            if (last_code[channel] >= 0 && btn != BTN_IDLE) {
              this->last_panel_press_ms_ = millis();
              if (btn == BTN_LIGHT_COLOR)
                this->last_light_press_ms_ = millis();
              this->on_panel_press_(btn);
            }
            last_code[channel] = code;
          }
        }
      }
      if (!this->channel_in_(this->active_channels_, channel)) {
        this->active_channels_.push_back(channel);
        ESP_LOGD(TAG, "Active channel 0x%02X", channel);
        if (this->channel_ >= 0 && channel == static_cast<uint8_t>(this->channel_)) {
          ESP_LOGW(TAG, "Channel conflict on 0x%02X — releasing", channel);
          this->channel_ = -1;
          this->detect_channel_state_ = 0;
        }
      }
      break;
    }
    default:
      break;
  }
}

bool SundanceSpa::validate_and_handle_frame_(const uint8_t *frame, size_t frame_size) {
  if (frame_size < 7 || frame[0] != M_STARTEND || frame[frame_size - 1] != M_STARTEND) {
    return false;
  }
  const uint8_t len_byte = frame[1];
  if (static_cast<size_t>(2 + len_byte) != frame_size) {
    return false;
  }
  const uint8_t expect = frame[frame_size - 2];
  const uint8_t calc = sundance_crc8(frame + 1, len_byte - 1);
  if (expect != calc) {
    this->diag_bad_crc_++;
    this->diag_last_bad_frame_.assign(frame, frame + frame_size);
    return false;
  }
  std::vector<uint8_t> copy(frame, frame + frame_size);
  this->handle_frame_(copy);
  return true;
}

bool SundanceSpa::try_frame_at_(size_t sof_index) {
  if (sof_index + 2 >= this->rx_window_.size())
    return false;
  if (this->rx_window_[sof_index] != M_STARTEND)
    return false;

  const uint8_t len_byte = this->rx_window_[sof_index + 1];
  if (len_byte < 5 || len_byte > 64)
    return false;

  const size_t frame_size = static_cast<size_t>(2 + len_byte);
  if (sof_index + frame_size > this->rx_window_.size())
    return false;
  if (this->rx_window_[sof_index + frame_size - 1] != M_STARTEND)
    return false;

  if (!this->validate_and_handle_frame_(&this->rx_window_[sof_index], frame_size)) {
    return false;
  }
  this->rx_window_.erase(this->rx_window_.begin(),
                         this->rx_window_.begin() + static_cast<long>(sof_index + frame_size));
  return true;
}

void SundanceSpa::scan_rx_window_() {
  int attempts = 0;
  while (!this->rx_window_.empty() && attempts < 48) {
    attempts++;

    size_t sof = 0;
    bool found = false;
    for (; sof < this->rx_window_.size(); sof++) {
      if (this->rx_window_[sof] == M_STARTEND) {
        found = true;
        break;
      }
    }
    if (!found) {
      this->rx_window_.clear();
      return;
    }
    if (sof > 0) {
      this->rx_window_.erase(this->rx_window_.begin(), this->rx_window_.begin() + static_cast<long>(sof));
    }
    if (this->rx_window_.size() < 2)
      return;

    const uint8_t len_byte = this->rx_window_[1];
    if (len_byte < 5 || len_byte > 64) {
      this->rx_window_.erase(this->rx_window_.begin());
      continue;
    }
    const size_t need = static_cast<size_t>(2 + len_byte);
    if (this->rx_window_.size() < need)
      return;

    if (this->try_frame_at_(0)) {
      continue;
    }

    this->rx_window_.erase(this->rx_window_.begin());
  }

  if (this->rx_window_.size() > RX_WINDOW_MAX) {
    const size_t drop = this->rx_window_.size() - (RX_WINDOW_MAX / 2);
    this->rx_window_.erase(this->rx_window_.begin(), this->rx_window_.begin() + static_cast<long>(drop));
  }
}

void SundanceSpa::push_rx_byte_(uint8_t byte) {
  if (this->rx_invert_) {
    byte = static_cast<uint8_t>(~byte);
  }
  this->rx_window_.push_back(byte);
  this->scan_rx_window_();
}

void SundanceSpa::loop() {
  int drained = 0;
  while (this->available() > 0 && drained < 1024) {
    int value = this->read();
    if (value < 0)
      break;
    this->push_rx_byte_(static_cast<uint8_t>(value));
    drained++;
  }
  this->diag_rx_bytes_ += drained;
  this->report_bus_stats_();
  this->loop_680_();

  static uint32_t last_comm_check = 0;
  if (millis() - last_comm_check > 5000) {
    last_comm_check = millis();
    bool ok = (this->last_status_ms_ != 0) && (millis() - this->last_status_ms_ < 15000);
    if (ok != this->last_communication_state_) {
      this->last_communication_state_ = ok;
      if (this->communication_binary_sensor_) {
        this->communication_binary_sensor_->publish_state(ok);
      }
    }
  }

  static uint32_t last_warn = 0;
  if (this->channel_ >= 0 && this->last_status_ms_ != 0 && (millis() - this->last_status_ms_ > 30000) &&
      (millis() - last_warn > 30000)) {
    last_warn = millis();
    ESP_LOGW(TAG, "No status update for %u s", (millis() - this->last_status_ms_) / 1000);
    this->decoder_notice_("No status from the spa for over 30 s");
  }

  if (this->channel_ < 0 && this->rx_frame_ok_ > 0 &&
      (this->last_channel_request_ms_ == 0 || millis() - this->last_channel_request_ms_ > 5000)) {
    this->send_channel_assignment_request_();
  }
}

}  // namespace sundance_spa
}  // namespace esphome
