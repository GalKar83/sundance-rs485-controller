#include "esphome/components/sundance_spa/select/sundance_select.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sundance_spa {

static const char *const TAG = "sundance_spa.select";

void SundanceLightsSelect::control(const std::string &value) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Lights select has no parent");
    return;
  }
  this->parent_->set_light_level(value);
}

void SundanceLightColorSelect::control(const std::string &value) {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Light Color select has no parent");
    return;
  }
  this->parent_->set_light_color(value);
}

}  // namespace sundance_spa
}  // namespace esphome
