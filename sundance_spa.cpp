#include "sundance_spa.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include <algorithm>
#include <cmath>
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
  return this->temp_scale_ == TempScale::CELSIUS ? 26.0f : 80.0f;
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
  if (this->temp_scale_ == TempScale::CELSIUS) {
    return raw / 2.0f;
  }
  return static_cast<float>(raw);
}

int SundanceSpa::display_to_native_temp_(float temp) const {
  if (this->temp_scale_ == TempScale::CELSIUS) {
    return static_cast<int>(std::lround(temp * 2.0f));
  }
  return static_cast<int>(std::lround(temp));
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
  if (manual)
    return "Running (Manual)";
  if (running)
    return "Running (Auto)";
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
  return this->wanted_light_level_ != NO_CHANGE_REQUESTED || this->wanted_light_mode_ != NO_CHANGE_REQUESTED;
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
    ESP_LOGW(TAG, "Giving up after %d command attempts", this->command_attempts_);
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

void SundanceSpa::service_command_retries_() {
  if (this->channel_ < 0 || this->check_counter_ > 0 || !this->tx_queue_.empty()) {
    return;
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
      cb(this->manual_circ_);
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
  if (this->heater_binary_sensor_)
    this->heater_binary_sensor_->publish_state(this->heater_on_);
  if (this->circ_binary_sensor_)
    this->circ_binary_sensor_->publish_state(this->circ_on_);
  if (this->circ_status_text_sensor_)
    this->circ_status_text_sensor_->publish_state(circ_status_name_(this->circ_on_, this->manual_circ_));
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
  if (data.size() < 15) {
    ESP_LOGW(TAG, "Decoded status too short (%u)", static_cast<unsigned>(data.size()));
    return;
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
  const int set_raw = data[8];
  const int cur_raw = data[5] ^ 2;
  const bool heater = ((data[10] >> 6) & 1) != 0;
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
  if (this->heater_on_ != heater || this->rest_mode_ != rest)
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
      if (this->prior_status_[i] != data[i]) {
        raw_changed = true;
        break;
      }
    }
  }
  this->prior_status_ = data;

  if (changed || raw_changed) {
    ESP_LOGD(TAG,
             "Status %02d:%02d set=%.1f cur=%.1f P1=%d P2=%d circ_run=%d circ_man=%d heat=%d rest=%d",
             time_hour, time_minute, this->set_temp_, this->current_temp_, pump0, pump1, circ_running,
             manual_circ, heater ? 1 : 0, rest ? 1 : 0);
    this->publish_states_();
  }
  this->service_command_retries_();
}

void SundanceSpa::parse_lights_update_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 10)
    return;
  const uint8_t *enc = &frame[5];
  const size_t enc_len = frame.size() - 7;
  if (enc_len < 2)
    return;
  std::vector<uint8_t> data = xormsg(enc, enc_len);
  if (data.size() < 5)
    return;

  const int brightness = data[1];
  const int mode = data[4];
  // Brightness is authoritative for on/off; some firmwares keep a color mode while dimmed.
  const bool on = brightness > 0;

  const bool changed = (on != this->light_on_) || (brightness != this->light_brightness_) ||
                       (mode != this->light_mode_);
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
}

void SundanceSpa::handle_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() < 7)
    return;

  this->rx_frame_ok_++;

  const uint8_t channel = frame[2];
  const uint8_t mtype = frame[4];

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
  }

  if (this->channel_ < 0 && this->rx_frame_ok_ > 0 &&
      (this->last_channel_request_ms_ == 0 || millis() - this->last_channel_request_ms_ > 5000)) {
    this->send_channel_assignment_request_();
  }
}

}  // namespace sundance_spa
}  // namespace esphome
