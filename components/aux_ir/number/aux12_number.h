#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/helpers.h"
#include "../aux_ir_climate.h"

namespace esphome::aux_ir {

class aux_irTimerNumber : public number::Number, public Parented<aux_ir_climate> {
 protected:
  // 0 cancels. The climate snaps the value to the remote's steps and publishes it.
  void control(float value) override { this->parent_->set_timer(value); }
};

}  // namespace esphome::aux_ir
