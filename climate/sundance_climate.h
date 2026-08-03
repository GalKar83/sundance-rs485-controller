#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sundance_spa/sundance_spa.h"

namespace esphome {
namespace sundance_spa {

class SundanceClimate : public climate::Climate, public Component {
 public:
  void setup() override;
  void dump_config() override {}
  void set_parent(SundanceSpa *parent) { this->parent_ = parent; }

 protected:
  SundanceSpa *parent_{nullptr};
  // User-requested spa setpoint. While set, the climate target dial stays on this
  // value instead of following the spa's intermediate set-temp as it steps.
  float pending_spa_target_{NAN};

  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
  float to_climate_temp_(float spa_temp) const;
  float from_climate_temp_(float climate_temp) const;
  void publish_climate_state_();
};

}  // namespace sundance_spa
}  // namespace esphome
