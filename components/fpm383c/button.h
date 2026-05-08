#pragma once

#include "esphome/components/button/button.h"

namespace esphome {
namespace fpm383c {

class FPM383C;

enum class ButtonAction : uint8_t { ENROLL, CANCEL, DELETE_ONE, DELETE_ALL };

class FPM383CButton : public button::Button {
 public:
  void set_parent(FPM383C *parent) { this->parent_ = parent; }
  void set_action(ButtonAction action) { this->action_ = action; }

 protected:
  void press_action() override;

  FPM383C *parent_{nullptr};
  ButtonAction action_{ButtonAction::ENROLL};
};

}  // namespace fpm383c
}  // namespace esphome
