#include "esphome/components/sundance_spa/button/sundance_button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sundance_spa {

static const char *const TAG = "sundance_spa.button";

void SundanceButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "No parent set for button %u", this->button_id_);
    return;
  }
  if (this->button_id_ == 3) {
    ESP_LOGD(TAG, "Circulation (ClearRay) press");
    this->parent_->on_toggle_circ();
    return;
  }
  if (this->button_id_ == 4) {
    ESP_LOGI(TAG, "Sync Clock pressed");
    this->parent_->request_clock_sync(true);
    return;
  }
  if (this->button_id_ == 5) {
    ESP_LOGI(TAG, "Read Spa Settings pressed");
    this->parent_->request_settings_read(false);
    return;
  }
  ESP_LOGW(TAG, "Unknown button id %u", this->button_id_);
}

}  // namespace sundance_spa
}  // namespace esphome
