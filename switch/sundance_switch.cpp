#include "esphome/components/sundance_spa/switch/sundance_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sundance_spa {

static const char *const TAG = "sundance_spa.switch";

void SundanceSwitch::setup() {
  if (this->parent_ == nullptr) {
    return;
  }

  uint8_t dp = 0;
  if (this->switch_id_ == 1)
    dp = DP_JET1;
  else if (this->switch_id_ == 2)
    dp = DP_JET2;

  if (dp != 0) {
    this->parent_->register_binary_sensor_callback(dp, [this](bool state) { this->publish_state(state); });
  }
}

void SundanceSwitch::write_state(bool state) {
  if (this->parent_ == nullptr) {
    return;
  }

  switch (this->switch_id_) {
    case 1:
      this->parent_->set_pump(0, state);
      break;
    case 2:
      this->parent_->set_pump(1, state);
      break;
    default:
      ESP_LOGW(TAG, "Unknown switch id %u", this->switch_id_);
      break;
  }
  // Do not optimistically publish — state is confirmed via status callbacks.
}

}  // namespace sundance_spa
}  // namespace esphome
