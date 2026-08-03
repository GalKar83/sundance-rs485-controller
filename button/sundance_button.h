#pragma once

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sundance_spa/sundance_spa.h"

namespace esphome {
namespace sundance_spa {

class SundanceButton : public Component, public button::Button {
 public:
  void press_action() override;
  void set_button_id(unsigned int id) { this->button_id_ = id; }
  void set_parent(SundanceSpa *parent) { this->parent_ = parent; }

 protected:
  SundanceSpa *parent_{nullptr};
  unsigned int button_id_{0};
};

}  // namespace sundance_spa
}  // namespace esphome
