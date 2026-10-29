#include "esphome/components/sundance_spa/climate/sundance_climate.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace sundance_spa {

static const char *const TAG = "sundance_spa.climate";

// ESPHome/Home Assistant climate entities are Celsius on the wire.
static float f_to_c_(float f) { return (f - 32.0f) * (5.0f / 9.0f); }
static float c_to_f_(float c) { return c * (9.0f / 5.0f) + 32.0f; }

float SundanceClimate::to_climate_temp_(float spa_temp) const {
  if (std::isnan(spa_temp)) {
    return spa_temp;
  }
  if (this->parent_ != nullptr && this->parent_->get_temperature_scale() == TempScale::FAHRENHEIT) {
    return f_to_c_(spa_temp);
  }
  return spa_temp;
}

float SundanceClimate::from_climate_temp_(float climate_temp) const {
  if (std::isnan(climate_temp)) {
    return climate_temp;
  }
  if (this->parent_ != nullptr && this->parent_->get_temperature_scale() == TempScale::FAHRENHEIT) {
    // HA sends °C; spa wants whole °F.
    return std::round(c_to_f_(climate_temp));
  }
  return std::round(climate_temp * 2.0f) / 2.0f;
}

void SundanceClimate::publish_climate_state_() {
  this->mode = climate::CLIMATE_MODE_HEAT;
  if (!std::isnan(this->pending_spa_target_)) {
    this->target_temperature = this->to_climate_temp_(this->pending_spa_target_);
  }
  this->publish_state();
}

void SundanceClimate::setup() {
  this->parent_->register_sensor_callback(DP_CURRENT_TEMP, [this](const float temp) {
    this->current_temperature = this->to_climate_temp_(temp);
    this->publish_climate_state_();
  });
  this->parent_->register_sensor_callback(DP_TARGET_TEMP, [this](const float temp) {
    if (!std::isnan(this->pending_spa_target_)) {
      if (std::fabs(temp - this->pending_spa_target_) < 0.25f) {
        ESP_LOGI(TAG, "Spa setpoint reached %.1f", temp);
        this->pending_spa_target_ = NAN;
        this->target_temperature = this->to_climate_temp_(temp);
      }
    } else {
      this->target_temperature = this->to_climate_temp_(temp);
    }
    this->publish_climate_state_();
  });
  this->parent_->register_binary_sensor_callback(DP_HEATER, [this](const bool on) {
    this->action = on ? climate::CLIMATE_ACTION_HEATING : climate::CLIMATE_ACTION_IDLE;
    this->publish_climate_state_();
  });

  this->mode = climate::CLIMATE_MODE_HEAT;
  this->action = climate::CLIMATE_ACTION_IDLE;
  ESP_LOGCONFIG(TAG, "Climate ready (spa scale %s, HA unit °C)",
                this->parent_->get_temperature_scale() == TempScale::CELSIUS ? "°C" : "°F");
}

climate::ClimateTraits SundanceClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  // Heat only: the spa has no heater-off mode, and ESPHome's traits start out with OFF already in the
  // set, so replace the set rather than add to it (OFF showed up in HA and did nothing, 2026-10-29).
  traits.set_supported_modes(climate::ClimateModeMask{climate::CLIMATE_MODE_HEAT});

  if (this->parent_ != nullptr && this->parent_->get_temperature_scale() == TempScale::FAHRENHEIT) {
    traits.set_visual_min_temperature(f_to_c_(this->parent_->get_min_temperature()));
    traits.set_visual_max_temperature(f_to_c_(this->parent_->get_max_temperature()));
    // Whole °F only. 1 °F == 5/9 °C on the climate entity's °C scale.
    traits.set_visual_temperature_step(5.0f / 9.0f);
  } else if (this->parent_ != nullptr) {
    traits.set_visual_min_temperature(this->parent_->get_min_temperature());
    traits.set_visual_max_temperature(this->parent_->get_max_temperature());
    traits.set_visual_temperature_step(this->parent_->get_temperature_step());
  } else {
    traits.set_visual_min_temperature(26.5f);
    traits.set_visual_max_temperature(40.0f);
    traits.set_visual_temperature_step(0.5f);
  }
  return traits;
}

void SundanceClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    auto mode = *call.get_mode();
    if (mode == climate::CLIMATE_MODE_OFF) {
      ESP_LOGW(TAG, "Climate OFF is not supported by the spa; use setpoint / rest mode instead");
      this->mode = climate::CLIMATE_MODE_HEAT;
      this->publish_state();
    } else {
      this->mode = climate::CLIMATE_MODE_HEAT;
      this->publish_state();
    }
  }

  if (call.get_target_temperature().has_value()) {
    const float climate_temp = *call.get_target_temperature();
    const float spa_temp = this->from_climate_temp_(climate_temp);
    ESP_LOGI(TAG, "Set target climate=%.2f°C → spa=%.1f", climate_temp, spa_temp);
    this->pending_spa_target_ = spa_temp;
    this->target_temperature = this->to_climate_temp_(spa_temp);
    this->mode = climate::CLIMATE_MODE_HEAT;
    this->publish_state();
    this->parent_->on_set_temp(spa_temp);
  }
}

}  // namespace sundance_spa
}  // namespace esphome
