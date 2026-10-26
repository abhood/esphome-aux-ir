#pragma once

#include "esphome/core/defines.h"
#include "esphome/components/climate_ir/climate_ir.h"
#include "aux_ir_protocol.h"

#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif

namespace esphome::aux_ir {

// Remote features that are not part of the standard climate entity.
enum class Feature : uint8_t { FEATURE_TURBO = 0, FEATURE_ECO, FEATURE_SLEEP, FEATURE_HEALTH, FEATURE_FAHRENHEIT, FEATURE_COUNT };
// One-shot remote buttons.
enum class ButtonType : uint8_t { BUTTON_DISPLAY = 0, BUTTON_ICLEAN };

class aux_ir_climate : public climate_ir::ClimateIR {
 public:
  aux_ir_climate();

  void setup() override;
  void dump_config() override;

  void set_use_fahrenheit(bool use_fahrenheit);

#ifdef USE_SWITCH
  void set_feature_switch(Feature feature, switch_::Switch *sw) { this->switches_[idx_(feature)] = sw; }
#endif
#ifdef USE_NUMBER
  void set_timer_number(number::Number *number) { this->timer_number_ = number; }
#endif

  // Called by the sub-entities.
  void set_feature(Feature feature, bool on);
  void press_button(ButtonType type);
  void set_timer(float hours);

 protected:
  void control(const climate::ClimateCall &call) override;
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

 private:
  static constexpr size_t idx_(Feature f) { return static_cast<size_t>(f); }

  // state <-> frame
  proto::State build_state_() const;
  void apply_received_(const proto::State &s);
  void send_(proto::Button button);

  // helpers
  proto::Mode proto_mode_(climate::ClimateMode mode) const;
  climate::ClimateMode climate_mode_(proto::Mode mode) const;
  int setpoint_() const;  // whole °C or °F, depending on the display unit
  void normalize_target_();
  void enforce_mode_rules_();
  bool feature_(Feature f) const { return this->features_[idx_(f)]; }
  void publish_extras_();

  // timer
  void start_timer_(float hours, bool on_timer);
  void clear_timer_();
  float timer_remaining_() const;
  void on_timer_expired_();
  static float quantize_timer_(float hours, bool round_up);

  // Per-mode memory, like the remote: each mode keeps its own setpoint and fan.
  struct ModeMemory {
    bool valid{false};
    float target{24.0f};
    climate::ClimateFanMode fan{climate::CLIMATE_FAN_AUTO};
  };
  static constexpr size_t MODE_SLOTS = 5;  // AUTO, COOL, DRY, HEAT, FAN
  static size_t mode_slot_(proto::Mode m) {
    switch (m) {
      case proto::Mode::MODE_COOL:
        return 1;
      case proto::Mode::MODE_DRY:
        return 2;
      case proto::Mode::MODE_HEAT:
        return 3;
      case proto::Mode::MODE_FAN:
        return 4;
      default:
        return 0;
    }
  }
  void remember_mode_();
  void recall_mode_(bool keep_target, bool keep_fan);
  ModeMemory memory_[MODE_SLOTS]{};

  bool features_[static_cast<size_t>(Feature::FEATURE_COUNT)]{};
  bool iclean_{false};
  climate::ClimateMode last_on_mode_{climate::CLIMATE_MODE_COOL};

  bool timer_active_{false};
  bool timer_is_on_timer_{false};
  uint32_t timer_deadline_ms_{0};

  proto::Frame last_tx_{};
  uint32_t last_tx_ms_{0};

#ifdef USE_SWITCH
  switch_::Switch *switches_[static_cast<size_t>(Feature::FEATURE_COUNT)]{};
#endif
#ifdef USE_NUMBER
  number::Number *timer_number_{nullptr};
#endif
};

}  // namespace esphome::aux_ir