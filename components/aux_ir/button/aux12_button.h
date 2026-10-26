#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"
#include "../aux_ir_climate.h"

namespace esphome::aux_ir {

class aux_irButton : public button::Button, public Parented<aux_ir_climate> {
 public:
  explicit aux_irButton(ButtonType type) : type_(type) {}

 protected:
  void press_action() override { this->parent_->press_button(this->type_); }

  ButtonType type_;
};

}  // namespace esphome::aux_ir
