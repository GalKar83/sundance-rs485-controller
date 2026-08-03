#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/sundance_spa/sundance_spa.h"

namespace esphome {
namespace sundance_spa {

class SundanceSwitch : public Component, public switch_::Switch {
 public:
  void setup() override;
  void write_state(bool state) override;

  void set_switch_id(unsigned int id) { this->switch_id_ = id; }
  void set_parent(SundanceSpa *parent) { this->parent_ = parent; }

 protected:
  SundanceSpa *parent_{nullptr};
  unsigned int switch_id_{0};
};

}  // namespace sundance_spa
}  // namespace esphome
