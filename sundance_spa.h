#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/core/gpio.h"

#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace sundance_spa {

static const uint8_t M_STARTEND = 0x7E;

static const uint8_t MSG_CLIENT_CTS = 0x00;
static const uint8_t MSG_CHANNEL_ASSIGNMENT_REQ = 0x01;
static const uint8_t MSG_CHANNEL_ASSIGNMENT_RESPONSE = 0x02;
static const uint8_t MSG_CHANNEL_ASSIGNMENT_ACK = 0x03;
static const uint8_t MSG_EXISTING_CLIENT_REQ = 0x04;
static const uint8_t MSG_EXISTING_CLIENT_RESPONSE = 0x05;
static const uint8_t MSG_CLEAR_TO_SEND = 0x06;
static const uint8_t MSG_NOTHING_TO_SEND = 0x07;

static const uint8_t MSG_STATUS_UPDATE = 0xC4;
static const uint8_t MSG_LIGHTS_UPDATE = 0xCA;
static const uint8_t MSG_CC_REQ = 0xCC;
static const uint8_t MSG_STATUS_UPDATE_ALT = 0x16;
static const uint8_t MSG_LIGHTS_UPDATE_ALT = 0x23;
static const uint8_t MSG_CC_REQ_ALT = 0x17;

static const uint8_t BTN_TEMP_UP = 225;
static const uint8_t BTN_TEMP_DOWN = 226;
static const uint8_t BTN_P1 = 228;
static const uint8_t BTN_P2 = 229;
static const uint8_t BTN_CLEAR_RAY = 239;
static const uint8_t BTN_LIGHT_ON = 241;
static const uint8_t BTN_LIGHT_COLOR = 242;

static const int NO_CHANGE_REQUESTED = -1;
// Wait enough status/light frames for the spa to apply a toggle before retrying.
static const int CHECKS_BEFORE_RETRY = 5;
static const int DETECT_CHANNEL_NOT_FOUND = 5;
static const int MAX_COMMAND_ATTEMPTS = 8;

static const uint8_t DP_HEATER = 21;
static const uint8_t DP_CIRC = 22;
static const uint8_t DP_REST = 23;
static const uint8_t DP_JET1 = 24;
static const uint8_t DP_JET2 = 25;
static const uint8_t DP_CURRENT_TEMP = 40;
static const uint8_t DP_TARGET_TEMP = 41;

static const size_t RX_WINDOW_MAX = 512;

enum class TempScale : uint8_t {
  FAHRENHEIT = 0,
  CELSIUS = 1,
};

// Quantized light levels matching HyperActiveJ / topside cycle (Off→High→Med→Low→Off).
// Names avoid Arduino GPIO macros LOW/HIGH.
enum class LightLevel : int {
  LEVEL_OFF = 0,
  LEVEL_LOW = 1,
  LEVEL_MEDIUM = 2,
  LEVEL_HIGH = 3,
};

class SundanceSpa : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_temp_sensor(sensor::Sensor *temp_sensor) { this->temp_sensor_ = temp_sensor; }
  void set_heater_binary_sensor(binary_sensor::BinarySensor *s) { this->heater_binary_sensor_ = s; }
  void set_circ_binary_sensor(binary_sensor::BinarySensor *s) { this->circ_binary_sensor_ = s; }
  void set_rest_binary_sensor(binary_sensor::BinarySensor *s) { this->rest_binary_sensor_ = s; }
  void set_circ_status_text_sensor(text_sensor::TextSensor *s) { this->circ_status_text_sensor_ = s; }
  void set_light_status_text_sensor(text_sensor::TextSensor *s) { this->light_status_sensor_ = s; }
  void set_lights_select(select::Select *s) { this->lights_select_ = s; }
  void set_light_color_select(select::Select *s) { this->light_color_select_ = s; }
  void set_temperature_scale(TempScale scale) { this->temp_scale_ = scale; }
  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }
  void set_rx_invert(bool invert) { this->rx_invert_ = invert; }
  void set_communication_binary_sensor(binary_sensor::BinarySensor *s) { this->communication_binary_sensor_ = s; }
  void set_pump(uint8_t pump, bool on);

  void register_sensor_callback(uint8_t datapoint, const std::function<void(float)> &func);
  void register_binary_sensor_callback(uint8_t datapoint, const std::function<void(bool)> &func);

  void on_set_temp(float temp);
  void on_toggle_circ();
  void set_light_level(const std::string &level_name);
  void set_light_color(const std::string &color_name);

  TempScale get_temperature_scale() const { return this->temp_scale_; }
  float get_min_temperature() const;
  float get_max_temperature() const;
  float get_temperature_step() const;

  static const char *light_mode_name(int mode);
  static const char *light_level_name(LightLevel level);
  static LightLevel brightness_to_level(int brightness);
  static int light_mode_id(const std::string &name);

 protected:
  std::vector<uint8_t> rx_window_;
  uint32_t last_status_ms_{0};
  uint32_t rx_frame_ok_{0};

  int channel_{-1};
  std::vector<uint8_t> discovered_channels_;
  std::vector<uint8_t> active_channels_;
  int detect_channel_state_{0};
  uint32_t last_channel_request_ms_{0};

  std::deque<std::vector<uint8_t>> tx_queue_;

  float current_temp_{NAN};
  float set_temp_{NAN};
  int wanted_temp_{NO_CHANGE_REQUESTED};
  int wanted_pump_[3]{NO_CHANGE_REQUESTED, NO_CHANGE_REQUESTED, NO_CHANGE_REQUESTED};
  int pump_status_[3]{0, 0, 0};
  int wanted_light_level_{NO_CHANGE_REQUESTED};
  int wanted_light_mode_{NO_CHANGE_REQUESTED};
  bool light_on_{false};
  bool heater_on_{false};
  bool circ_on_{false};      // circ pump physically running (auto or manual)
  bool manual_circ_{false};  // user/manual circ mode (switch target)
  bool rest_mode_{false};
  int light_mode_{-1};
  int light_brightness_{-1};
  int check_counter_{0};
  int light_check_counter_{0};
  int command_attempts_{0};
  std::vector<uint8_t> prior_status_;

  TempScale temp_scale_{TempScale::FAHRENHEIT};
  bool rx_invert_{false};
  GPIOPin *flow_control_pin_{nullptr};

  sensor::Sensor *temp_sensor_{nullptr};
  binary_sensor::BinarySensor *heater_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *circ_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *rest_binary_sensor_{nullptr};
  text_sensor::TextSensor *circ_status_text_sensor_{nullptr};
  text_sensor::TextSensor *light_status_sensor_{nullptr};
  select::Select *lights_select_{nullptr};
  select::Select *light_color_select_{nullptr};
  binary_sensor::BinarySensor *communication_binary_sensor_{nullptr};
  bool last_communication_state_{false};

  static const char *circ_status_name_(bool running, bool manual);

  std::vector<std::function<void(float)>> current_temp_cbs_;
  std::vector<std::function<void(float)>> target_temp_cbs_;
  std::vector<std::function<void(bool)>> heater_cbs_;
  std::vector<std::function<void(bool)>> circ_cbs_;
  std::vector<std::function<void(bool)>> rest_cbs_;
  std::vector<std::function<void(bool)>> jet1_cbs_;
  std::vector<std::function<void(bool)>> jet2_cbs_;

  static uint8_t sundance_crc8(const uint8_t *data, size_t len);
  static std::vector<uint8_t> xormsg(const uint8_t *data, size_t len);
  void set_tx_enable_(bool enable);
  void send_raw_(const std::vector<uint8_t> &frame);
  void queue_cc_button_(uint8_t button);
  void clear_pending_commands_();
  void send_channel_assignment_request_();
  void send_channel_assignment_ack_();
  void send_existing_client_response_();
  void reset_channel_state_();
  void push_rx_byte_(uint8_t byte);
  void scan_rx_window_();
  bool try_frame_at_(size_t sof_index);
  bool validate_and_handle_frame_(const uint8_t *frame, size_t frame_size);
  void handle_frame_(const std::vector<uint8_t> &frame);
  void parse_status_update_(const std::vector<uint8_t> &frame);
  void parse_lights_update_(const std::vector<uint8_t> &frame);
  void publish_states_();
  void publish_light_selects_();
  void publish_light_status_();
  bool light_seek_active_() const;
  void service_command_retries_();
  void service_light_retries_();
  bool channel_in_(const std::vector<uint8_t> &list, uint8_t ch) const;
  float native_to_display_temp_(int raw) const;
  int display_to_native_temp_(float temp) const;
};

}  // namespace sundance_spa
}  // namespace esphome
