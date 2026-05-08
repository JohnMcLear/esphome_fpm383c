#include "button.h"
#include "fpm383c.h"

namespace esphome {
namespace fpm383c {

void FPM383CButton::press_action() {
  if (this->parent_ == nullptr) return;
  switch (this->action_) {
    case ButtonAction::ENROLL:     this->parent_->enroll();     break;
    case ButtonAction::CANCEL:     this->parent_->cancel();     break;
    case ButtonAction::DELETE_ONE: this->parent_->delete_one(); break;
    case ButtonAction::DELETE_ALL: this->parent_->delete_all(); break;
  }
}

}  // namespace fpm383c
}  // namespace esphome
