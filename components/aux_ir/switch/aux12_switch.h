#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/helpers.h"
#include "../aux_ir_climate.h"

namespace esphome::aux_ir {

class aux_irSwitch : public switch_::Switch, public Parented<aux_ir_climate> {
 public:
  explicit aux_irSwitch(Feature feature) : feature_(feature) {}

 protected:
  // The climate decides whether the change is allowed and publishes the real state.
  void write_state(bool state) override { this->parent_->set_feature(this->feature_, state); }

  Feature feature_;
};

}  // namespace esphome::aux_ir
