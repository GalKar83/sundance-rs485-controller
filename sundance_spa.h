#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/core/gpio.h"
#include "esphome/core/preferences.h"
#include "esphome/components/time/real_time_clock.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <utility>
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
static const uint8_t BTN_LIGHT_COLOR = 242;  // 680: the single Light button
static const uint8_t BTN_MENU = 254;
// Activity items (run timers, Active / Session binary sensors).
static const int ACTIVITY_COUNT = 8;
static const uint8_t BTN_IDLE = 224;  // what the topside sends continuously when nothing is pressed

// 680 topside screen codes (status f13)
static const int SCREEN_MAIN = 78;  // codes below are AFTER removing the XOR key (see screen_())
static const int SCREEN_TIME = 84;
static const int SCREEN_TIME_HOUR = 85;
static const int SCREEN_TIME_MINUTE = 83;
static const int SCREEN_TIME_AMPM = 80;
static const int SCREEN_DATE = 81;
static const int SCREEN_DATE_SCROLL = 110;
static const int SCREEN_DATE_YEAR = 111;
static const int SCREEN_DATE_MONTH = 108;
static const int SCREEN_DATE_DAY = 109;
static const int SCREEN_PF = 73;
static const int SCREEN_PF_START = 70;
static const int SCREEN_PF_START_EDIT = 71;
static const int SCREEN_PF_CYCLES = 113;
static const int SCREEN_PF_DURATION = 68;
static const int SCREEN_UV = 90;
static const int SCREEN_UV_DAYS = 91;  // ClearRay days shown (first Warmer/Cooler); f15 see uv_days_from_()
static const int SCREEN_UV_EDIT = 88;  // further presses: days being chosen
static const int UV_OFF = -2;          // ClearRay timer set to Off (publish_uv_days_ / uv_days_from_)
static const int SCREEN_LANG = 105;
static const int SCREEN_LANG_SHOW = 102;  // first Warmer/Cooler: current language in f15
static const int SCREEN_LANG_EDIT = 103;  // further presses: language being chosen in f15
static const int SCREEN_FC = 95;
static const int SCREEN_FC_SHOW = 92;  // FC value screens; f15 = days, 128 = Off
static const int SCREEN_FC_EDIT = 93;
// LOCK menu option screens (Jets 1 on one of them sets that lock; verified 2026-10-23/25)
static const int SCREEN_LOCK_SL = 98;   // Settings Lock
static const int SCREEN_LOCK_SVL = 13;  // Service Lock
static const uint8_t BTN_MENU_HOLD = 209;  // long Menu press: unlock

static const int NO_CHANGE_REQUESTED = -1;
// Jet 1 modes on the 680 (one two-speed pump): Off, manual Low ("Circulation"), High.
static const int JET1_OFF = 0;
static const int JET1_LOW = 1;
static const int JET1_HIGH = 2;
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
  void set_pumps_running_binary_sensor(binary_sensor::BinarySensor *s) { this->pumps_running_sensor_ = s; }
  void set_circulation_auto_binary_sensor(binary_sensor::BinarySensor *s) { this->circ_auto_sensor_ = s; }
  void set_h2o_change_sensor(text_sensor::TextSensor *s) { this->h2o_change_sensor_ = s; }
  void set_filter_change_sensor(text_sensor::TextSensor *s) { this->filter_change_sensor_ = s; }
  void set_uv_change_sensor(text_sensor::TextSensor *s) { this->uv_change_sensor_ = s; }
  void add_raw_status_sensor(int field, sensor::Sensor *s) { this->raw_status_sensors_.push_back({field, s}); }
  void set_screen_key_sensor(sensor::Sensor *s) { this->screen_key_sensor_ = s; }
  void set_jet1_run_sensor(sensor::Sensor *s) { this->jet1_run_sensor_ = s; }
  void set_jet2_run_sensor(sensor::Sensor *s) { this->jet2_run_sensor_ = s; }
  void set_light_run_sensor(sensor::Sensor *s) { this->light_run_sensor_ = s; }
  void set_circ_run_sensor(sensor::Sensor *s) { this->circ_run_sensor_ = s; }
  void set_heater_run_sensor(sensor::Sensor *s) { this->heater_run_sensor_ = s; }
  void add_activity(int item, binary_sensor::BinarySensor *active, binary_sensor::BinarySensor *session,
                    uint32_t session_min_s) {
    if (item < 0 || item >= ACTIVITY_COUNT)
      return;
    this->activity_[item].active = active;
    this->activity_[item].session = session;
    this->activity_[item].session_min_ms = session_min_s * 1000UL;
  }
  void set_panel_button_sensor(text_sensor::TextSensor *s) { this->panel_button_sensor_ = s; }
  void set_raw_lights_text_sensor(text_sensor::TextSensor *s) { this->raw_lights_text_sensor_ = s; }
  void set_msg_types_text_sensor(text_sensor::TextSensor *s) { this->msg_types_text_sensor_ = s; }
  void set_raw_other_text_sensor(text_sensor::TextSensor *s) { this->raw_other_text_sensor_ = s; }
  void set_time_text_sensor(text_sensor::TextSensor *s) { this->time_text_sensor_ = s; }
  void set_heater_mode_text_sensor(text_sensor::TextSensor *s) { this->heater_mode_text_sensor_ = s; }
  void set_clearray_text_sensor(text_sensor::TextSensor *s) { this->clearray_text_sensor_ = s; }
  void set_language_text_sensor(text_sensor::TextSensor *s) { this->language_text_sensor_ = s; }
  void set_lock_text_sensor(text_sensor::TextSensor *s) { this->lock_text_sensor_ = s; }
  void set_temp_format_text_sensor(text_sensor::TextSensor *s) { this->temp_format_text_sensor_ = s; }
  void set_date_text_sensor(text_sensor::TextSensor *s) { this->date_text_sensor_ = s; }
  void set_clock_sync_text_sensor(text_sensor::TextSensor *s) { this->clock_sync_text_sensor_ = s; }
  void set_decoder_notice_text_sensor(text_sensor::TextSensor *s) { this->decoder_notice_text_sensor_ = s; }
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  void set_clock_auto_sync(bool b) { this->clock_auto_sync_ = b; }

  // 680 additions (sundance_680.cpp)
  void set_light_680(const std::string &name);
  static const char *light680_name(int index);
  void request_clock_sync(bool manual);
  void request_settings_read(bool with_sync);
  // Runtime switch for the [DIAG] bus/byte logging (HA "Diagnostics" switch); no reflash needed.
  void set_diagnostics(bool enabled) { this->diag_enabled_ = enabled; }
  void set_pf_start_text_sensor(text_sensor::TextSensor *s) { this->pf_start_text_sensor_ = s; }
  void set_pf_cycles_sensor(sensor::Sensor *s) { this->pf_cycles_sensor_ = s; }
  void set_pf_duration_sensor(sensor::Sensor *s) { this->pf_duration_sensor_ = s; }
  void set_pump(uint8_t pump, bool on);
  void set_jet1_mode(int mode);

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
  int wanted_jet1_{NO_CHANGE_REQUESTED};
  int jet1_mode_() const;
  bool auto_filtration_() const;
  int pump_status_[3]{0, 0, 0};
  int wanted_light_level_{NO_CHANGE_REQUESTED};
  int wanted_light_mode_{NO_CHANGE_REQUESTED};
  bool light_on_{false};
  bool heater_on_{false};
  bool circ_on_{false};      // circ pump physically running (auto or manual)
  bool manual_circ_{false};  // user/manual circ mode (switch target)
  bool circ_cmd_{false};      // Jet 1 low speed commanded (status f1 bit5)
  bool filter_cycle_{false};  // scheduled filtration cycle active (status f10 bit7)
  bool rest_mode_{false};
  int light_mode_{-1};
  int light_brightness_{-1};
  int check_counter_{0};
  int light_check_counter_{0};
  int command_attempts_{0};
  std::vector<uint8_t> prior_status_;
  std::vector<uint8_t> prior_lights_;

  // Bus diagnostics (680-series bring-up)
  bool diag_enabled_{true};  // on until the HA switch restores its saved state
  uint32_t diag_rx_bytes_{0};
  uint32_t diag_bad_crc_{0};
  uint32_t diag_last_report_ms_{0};
  std::vector<uint8_t> diag_last_bad_frame_;
  std::vector<uint8_t> diag_seen_types_;
  void log_decoded_diff_(const char *label, const std::vector<uint8_t> &prev, const std::vector<uint8_t> &cur);
  void report_bus_stats_();

  TempScale temp_scale_{TempScale::FAHRENHEIT};
  bool spa_fahrenheit_{false};  // unit the spa panel is set to (status f14 bit6)
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
  binary_sensor::BinarySensor *pumps_running_sensor_{nullptr};  // any pump running, any reason
  binary_sensor::BinarySensor *circ_auto_sensor_{nullptr};      // low speed run started by the controller
  text_sensor::TextSensor *h2o_change_sensor_{nullptr};
  text_sensor::TextSensor *filter_change_sensor_{nullptr};
  std::vector<std::pair<int, sensor::Sensor *>> raw_status_sensors_;  // decoding aid: raw status bytes
  sensor::Sensor *screen_key_sensor_{nullptr};
  sensor::Sensor *jet1_run_sensor_{nullptr};
  sensor::Sensor *jet2_run_sensor_{nullptr};
  sensor::Sensor *light_run_sensor_{nullptr};
  sensor::Sensor *circ_run_sensor_{nullptr};
  sensor::Sensor *heater_run_sensor_{nullptr};
  struct Activity {
    binary_sensor::BinarySensor *active{nullptr};
    binary_sensor::BinarySensor *session{nullptr};
    uint32_t session_min_ms{0};
  };
  Activity activity_[ACTIVITY_COUNT];
  uint32_t run_since_ms_[ACTIVITY_COUNT]{};
  // Confirmed (2 s) state of each activity item and since when the raw state differs from it (0 = same).
  bool act_on_[ACTIVITY_COUNT]{};
  uint32_t act_change_ms_[ACTIVITY_COUNT]{};
  bool act_init_{false};
  // Derived states are published only once they have held DEBOUNCE_MS: the controller switches its
  // flags one or two status frames apart (e.g. at the end of a filter cycle), which otherwise shows as
  // 0.3-1.7 s blips (Jet 1 Active / "Circulation (Heating)" seen 2026-10-24..27). Switches, lights and
  // Panel Button stay immediate.
  struct DebounceText {
    text_sensor::TextSensor *sensor{nullptr};
    std::string want;
    uint32_t since{0};
    bool pending{false};
  };
  struct DebounceBin {
    binary_sensor::BinarySensor *sensor{nullptr};
    bool want{false};
    uint32_t since{0};
    bool pending{false};
  };
  DebounceText circ_status_db_, clearray_db_;
  DebounceBin heater_db_, pumps_running_db_, circ_auto_db_;
  void debounce_(DebounceText &d, const std::string &v);
  void debounce_(DebounceBin &d, bool v);
  void flush_debounced_();
  uint32_t run_timer_tick_ms_{0};
  void service_run_timers_();
  text_sensor::TextSensor *panel_button_sensor_{nullptr};
  text_sensor::TextSensor *raw_lights_text_sensor_{nullptr};
  text_sensor::TextSensor *msg_types_text_sensor_{nullptr};
  text_sensor::TextSensor *raw_other_text_sensor_{nullptr};
  std::map<uint16_t, std::string> raw_other_last_;
  uint32_t raw_other_pub_ms_{0};
  text_sensor::TextSensor *uv_change_sensor_{nullptr};  // read from the UV menu, then counted down by the screen key
  text_sensor::TextSensor *time_text_sensor_{nullptr};
  text_sensor::TextSensor *heater_mode_text_sensor_{nullptr};
  text_sensor::TextSensor *clearray_text_sensor_{nullptr};
  text_sensor::TextSensor *language_text_sensor_{nullptr};
  text_sensor::TextSensor *lock_text_sensor_{nullptr};
  text_sensor::TextSensor *temp_format_text_sensor_{nullptr};
  text_sensor::TextSensor *date_text_sensor_{nullptr};
  text_sensor::TextSensor *clock_sync_text_sensor_{nullptr};
  text_sensor::TextSensor *decoder_notice_text_sensor_{nullptr};
  time::RealTimeClock *time_{nullptr};
  bool clock_auto_sync_{true};

  // 680 state
  int spa_hour_{0};
  int spa_minute_{0};
  int light_speed_{0};
  int wanted_light680_{NO_CHANGE_REQUESTED};
  int light680_expect_{NO_CHANGE_REQUESTED};
  int light680_presses_{0};
  uint32_t light680_sent_ms_{0};
  uint32_t last_light_press_ms_{0};
  uint32_t last_panel_press_ms_{0};
  int sync_step_{0};
  uint32_t sync_step_ms_{0};
  uint32_t sync_press_ms_{0};
  uint32_t sync_started_ms_{0};
  uint32_t last_sync_attempt_ms_{0};
  uint32_t last_auto_check_ms_{0};
  int sync_nav_index_{0};
  int sync_count_{0};
  int sync_target_pm_{0};
  int sync_target_min_{0};   // minute to set (from the target time of day)
  int sync_delta_min_{0};    // minutes to move the spa clock (when its offset is known)
  bool sync_by_offset_{false};  // false: no offset measured yet -> target is HA's time
  uint32_t sync_edit_ms_{0};    // when the time edit started (the spa clock stands still until the save)
  int sync_expect_s_{0};        // offset expected after the save
  int sync_mismatch_count_{0};
  bool sync_confirmed_{false};
  bool sync_manual_{false};
  text_sensor::TextSensor *pf_start_text_sensor_{nullptr};
  sensor::Sensor *pf_cycles_sensor_{nullptr};
  sensor::Sensor *pf_duration_sensor_{nullptr};
  int menu_prev_screen_{-1};
  int menu_prev_value_{0};
  bool uv_display_pending_{false};
  void publish_language_(int value);
  int routine_mask_{0};
  int read_task_{0};
  int last_routine_yday_{-1};
  bool boot_read_done_{false};
  void publish_uv_days_(int days);
  void observe_menu_values_();
  bool clock_needs_sync_() const;
  void start_next_task_();
  int screen_key_{0};
  int uv_days_{-1};  // ClearRay counter as last known (0 = Off or due, -1 = unknown)
  int f13_steady_raw_{-1};  // raw screen code and since when it has not changed
  uint32_t f13_steady_ms_{0};
  uint32_t last_own_press_ms_{0};
  int screen_() const;
  bool menu_editing_() const;
  bool menu_in_use_() const;
  void learn_screen_key_();
  // Service counters: FC Off and FC expired both arrive as f3 = 0xFA; told apart by how 0 was reached
  // (natural 1 -> 0 = due) or by the FC menu value (128 = Off). 0 unknown, 1 Off, 2 Change Now.
  int fc_zero_kind_{0};
  int fc_last_days_{-1};
  void publish_counters_();
  // Settings / Service Lock have no status bit of their own: tracked from the panel events that set
  // and clear them, each confirmed by its f14 bit flipping (bit1 SL, bit2 SVL) at that moment.
  bool sl_on_{false};
  bool svl_on_{false};
  int lock_event_{0};  // 1 SL set, 2 SVL set, 3 unlock (Menu Hold)
  uint32_t lock_event_ms_{0};
  uint8_t lock_event_f14_{0};
  ESPPreferenceObject lock_pref_;
  void on_panel_press_(uint8_t btn);
  void service_lock_event_();
  // Jet 2 started by the controller (daily cleanup / unlock purge), not by a press -> "Cleanup".
  bool jet2_auto_{false};
  // The power-up / Service Lock purge runs Jet 1 low with the *manual* circulation bit (f1 bit7) set
  // although nobody pressed Circulation: treated as automatic circulation, not as the manual switch.
  bool manual_auto_{false};
  uint32_t last_circ_press_ms_{0};  // last Circulation press, topside or ours
  // Spa clock offset to HA in seconds, measured at each spa minute rollover (positive = spa ahead).
  int spa_offset_s_{0};
  bool spa_offset_valid_{false};
  int spa_tod_prev_min_{-1};
  uint32_t main_screen_since_ms_{0};  // since when the topside shows the main screen (0 = not now)
  bool sync_eval_pending_{false};  // judge the first offset measured after a sync
  int sync_fail_streak_{0};        // syncs in a row that left the offset at a minute or more
  void track_spa_offset_();

  static int light680_index_from_name_(const std::string &name);
  int light680_index_() const;
  void service_light680_();
  bool decode_spa_date_(int &year, int &month, int &day) const;
  void update_680_status_();
  bool clock_sync_active_() const;
  void set_clock_sync_status_(const std::string &s);
  // Something the decoder could not handle: logged, published, and cleared to "OK" after an hour, so the
  // same problem can notify again later but not more than about once an hour.
  void decoder_notice_(const std::string &s);
  uint32_t bad_main_screen_ms_{0};  // since when the quiet topside screen disagrees with the key (0 = it fits)
  void abort_clock_sync_(const char *why);
  void sync_press_(uint8_t btn);
  void sync_goto_(int step);
  void service_clock_sync_();
  void loop_680_();
  void queue_press_(uint8_t button);
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
