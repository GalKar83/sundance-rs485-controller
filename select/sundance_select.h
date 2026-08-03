#pragma once

#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sundance_spa/sundance_spa.h"

namespace esphome {
namespace sundance_spa {

class SundanceLightsSelect : public select::Select, public Component {
 public:
  void set_parent(SundanceSpa *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;
  SundanceSpa *parent_{nullptr};
};

class SundanceLightColorSelect : public select::Select, public Component {
 public:
  void set_parent(SundanceSpa *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;
  SundanceSpa *parent_{nullptr};
};

}  // namespace sundance_spa
}  // namespace esphome
