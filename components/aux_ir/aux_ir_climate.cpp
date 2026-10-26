#include "aux_ir_climate.h"

#include <cmath>
#include <cstdio>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::aux_ir {

static const char *const TAG = "aux_ir";
static const char *const TIMER_ID = "aux_ir_timer";
static const char *const TIMER_TICK_ID = "aux_ir_timer_tick";
static constexpr uint32_t ECHO_WINDOW_MS = 1500;
static constexpr uint32_t MS_PER_HOUR = 3600000UL;

static const char *const FEATURE_NAMES[] = {"Turbo", "Eco", "Sleep", "Health", "Fahrenheit"};

static void format_frame(const proto::Frame &f, char *buf) {  // buf >= 3 * FRAME_LEN
  for (size_t i = 0; i < proto::FRAME_LEN; i++)
    snprintf(buf + 3 * i, 4, i + 1 < proto::FRAME_LEN ? "%02X " : "%02X", f[i]);
}

aux_ir_climate::aux_ir_climate()
    : climate_ir::ClimateIR(proto::C_MIN, proto::C_MAX, 1.0f, true, true,
                            climate::ClimateFanModeMask{climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
                                                        climate::CLIMATE_FAN_MEDIUM, climate::CLIMATE_FAN_HIGH},
                            climate::ClimateSwingModeMask{climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL,
                                                          climate::CLIMATE_SWING_HORIZONTAL,
                                                          climate::CLIMATE_SWING_BOTH}) {
  // Remote defaults: Dry starts on Low, Fan mode on High.
  this->memory_[mode_slot_(proto::Mode::MODE_DRY)].fan = climate::CLIMATE_FAN_LOW;
  this->memory_[mode_slot_(proto::Mode::MODE_FAN)].fan = climate::CLIMATE_FAN_HIGH;
}

void aux_ir_climate::set_use_fahrenheit(bool use_fahrenheit) {
  this->features_[idx_(Feature::FEATURE_FAHRENHEIT)] = use_fahrenheit;
  if (use_fahrenheit) {
    // 60–90 °F, so Home Assistant doesn't clamp 60 °F to 61 °F
    this->minimum_temperature_ = (proto::F_MIN - 32) * 5.0f / 9.0f;
    this->maximum_temperature_ = (proto::F_MAX - 32) * 5.0f / 9.0f;
  }
}

// ---------------------------------------------------------------- setup / config
void aux_ir_climate::setup() {
  climate_ir::ClimateIR::setup();
#ifdef USE_SWITCH
  for (size_t i = 0; i < idx_(Feature::FEATURE_COUNT); i++) {
    if (this->switches_[i] == nullptr)
      continue;
    auto restored = this->switches_[i]->get_initial_state_with_restore_mode();
    if (restored.has_value())
      this->features_[i] = *restored;
  }
#endif
  if (this->mode != climate::CLIMATE_MODE_OFF)
    this->last_on_mode_ = this->mode;
  this->normalize_target_();
  this->enforce_mode_rules_();
  this->publish_state();
  this->publish_extras_();
}

void aux_ir_climate::dump_config() {
  climate_ir::ClimateIR::dump_config();
  ESP_LOGCONFIG(TAG, "  Protocol: AUX 13-byte\n  Display unit: %s", this->feature_(Feature::FEATURE_FAHRENHEIT) ? "°F" : "°C");
#ifdef USE_SWITCH
  for (size_t i = 0; i < idx_(Feature::FEATURE_COUNT); i++) {
    if (this->switches_[i] != nullptr) {
      ESP_LOGCONFIG(TAG, "  %s switch: yes", FEATURE_NAMES[i]);
    }
  }
#endif
#ifdef USE_NUMBER
  ESP_LOGCONFIG(TAG, "  Timer number: %s", YESNO(this->timer_number_ != nullptr));
#endif
}

// ---------------------------------------------------------------- mode / temperature helpers
proto::Mode aux_ir_climate::proto_mode_(climate::ClimateMode mode) const {
  switch (mode) {
    case climate::CLIMATE_MODE_HEAT:
      return proto::Mode::MODE_HEAT;
    case climate::CLIMATE_MODE_DRY:
      return proto::Mode::MODE_DRY;
    case climate::CLIMATE_MODE_FAN_ONLY:
      return proto::Mode::MODE_FAN;
    case climate::CLIMATE_MODE_HEAT_COOL:
    case climate::CLIMATE_MODE_AUTO:
      return proto::Mode::MODE_AUTO;
    default:
      return proto::Mode::MODE_COOL;
  }
}

climate::ClimateMode aux_ir_climate::climate_mode_(proto::Mode mode) const {
  switch (mode) {
    case proto::Mode::MODE_HEAT:
      return climate::CLIMATE_MODE_HEAT;
    case proto::Mode::MODE_DRY:
      return climate::CLIMATE_MODE_DRY;
    case proto::Mode::MODE_FAN:
      return climate::CLIMATE_MODE_FAN_ONLY;
    case proto::Mode::MODE_AUTO:
      return this->modes_.count(climate::CLIMATE_MODE_HEAT_COOL) ? climate::CLIMATE_MODE_HEAT_COOL
                                                                 : climate::CLIMATE_MODE_AUTO;
    default:
      return climate::CLIMATE_MODE_COOL;
  }
}

int aux_ir_climate::setpoint_() const {
  if (this->feature_(Feature::FEATURE_FAHRENHEIT)) {
    int f = static_cast<int>(std::lround(this->target_temperature * 9.0f / 5.0f + 32.0f));
    return clamp(f, proto::F_MIN, proto::F_MAX);
  }
  return clamp(static_cast<int>(std::lround(this->target_temperature)), proto::C_MIN, proto::C_MAX);
}

// Snap the target to a value the remote can show, in the active display unit.
void aux_ir_climate::normalize_target_() {
  if (std::isnan(this->target_temperature))
    this->target_temperature = 24.0f;
  int sp = this->setpoint_();
  this->target_temperature = this->feature_(Feature::FEATURE_FAHRENHEIT) ? (sp - 32) * 5.0f / 9.0f : static_cast<float>(sp);
}

// Remote behaviour: Turbo only in Cool/Heat, Eco only in Cool, Sleep not in Fan, no Auto fan in Fan mode.
// (In Heat the Eco button toggles aux heat instead; this unit has no aux heater.)
void aux_ir_climate::enforce_mode_rules_() {
  const proto::Mode m = this->proto_mode_(this->mode);
  if (m != proto::Mode::MODE_COOL && m != proto::Mode::MODE_HEAT)
    this->features_[idx_(Feature::FEATURE_TURBO)] = false;
  if (m != proto::Mode::MODE_COOL)
    this->features_[idx_(Feature::FEATURE_ECO)] = false;
  if (m == proto::Mode::MODE_FAN)
    this->features_[idx_(Feature::FEATURE_SLEEP)] = false;
  if (this->mode == climate::CLIMATE_MODE_FAN_ONLY && this->fan_mode == climate::CLIMATE_FAN_AUTO)
    this->fan_mode = climate::CLIMATE_FAN_HIGH;
}

void aux_ir_climate::remember_mode_() {
  if (this->mode == climate::CLIMATE_MODE_OFF)
    return;
  ModeMemory &m = this->memory_[mode_slot_(this->proto_mode_(this->mode))];
  m.valid = true;
  m.target = this->target_temperature;
  m.fan = this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
}

void aux_ir_climate::recall_mode_(bool keep_target, bool keep_fan) {
  const ModeMemory &m = this->memory_[mode_slot_(this->proto_mode_(this->mode))];
  if (!keep_fan)
    this->fan_mode = m.fan;  // never visited: the remote's default (Auto; Dry Low; Fan High)
  // FAN / AUTO have no setpoint on the remote; keep the current one there
  if (!keep_target && m.valid && proto::mode_has_setpoint(this->proto_mode_(this->mode)))
    this->target_temperature = m.target;
}

// ---------------------------------------------------------------- state -> frame
proto::State aux_ir_climate::build_state_() const {
  proto::State s;
  const bool on = this->mode != climate::CLIMATE_MODE_OFF;
  const climate::ClimateMode active = on ? this->mode : this->last_on_mode_;

  s.power = on;
  s.mode = this->proto_mode_(active);

  const int sp = this->setpoint_();
  s.fahrenheit = this->feature_(Feature::FEATURE_FAHRENHEIT);
  s.temp_f = s.fahrenheit ? static_cast<uint8_t>(sp) : 0;
  s.temp_c = static_cast<uint8_t>(s.fahrenheit ? proto::f_to_c(sp) : sp);

  switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
    case climate::CLIMATE_FAN_LOW:
      s.fan = proto::Fan::FAN_LOW;
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      s.fan = proto::Fan::FAN_MEDIUM;
      break;
    case climate::CLIMATE_FAN_HIGH:
      s.fan = proto::Fan::FAN_HIGH;
      break;
    default:
      s.fan = s.mode == proto::Mode::MODE_FAN ? proto::Fan::FAN_HIGH : proto::Fan::FAN_AUTO;
      break;
  }

  s.vswing = this->swing_mode == climate::CLIMATE_SWING_VERTICAL || this->swing_mode == climate::CLIMATE_SWING_BOTH;
  s.hswing = this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL || this->swing_mode == climate::CLIMATE_SWING_BOTH;

  if (on) {
    s.turbo = this->feature_(Feature::FEATURE_TURBO);
    s.eco = this->feature_(Feature::FEATURE_ECO);
    s.sleep = this->feature_(Feature::FEATURE_SLEEP);
    s.health = this->feature_(Feature::FEATURE_HEALTH);
    s.aux_heat = s.mode == proto::Mode::MODE_HEAT;  // the remote always sets it in Heat
  }

  if (this->iclean_ && !on) {  // iClean frame, as sent by the remote
    s.iclean = true;
    s.mode = proto::Mode::MODE_FAN;
    s.fan = proto::Fan::FAN_HIGH;
  }

  if (this->timer_active_) {
    const float h = quantize_timer_(this->timer_remaining_(), true);
    const int whole = static_cast<int>(h);
    s.timer_hours = static_cast<uint8_t>(whole);
    s.timer_minutes = (h - whole) > 0.25f ? 30 : 0;
    s.on_timer = this->timer_is_on_timer_;
    s.off_timer = !this->timer_is_on_timer_;
  }
  return s;
}

void aux_ir_climate::send_(proto::Button button) {
  const proto::Frame f = proto::encode(this->build_state_(), button);

  auto call = this->transmitter_->transmit();
  auto *data = call.get_data();
  data->set_carrier_frequency(proto::CARRIER_HZ);
  data->reserve(2 + proto::FRAME_LEN * 16 + 1);
  data->item(proto::HEADER_MARK, proto::HEADER_SPACE);
  for (uint8_t byte : f) {
    for (uint8_t bit = 0; bit < 8; bit++)
      data->item(proto::BIT_MARK, (byte >> bit) & 1 ? proto::ONE_SPACE : proto::ZERO_SPACE);
  }
  data->mark(proto::BIT_MARK);
  call.perform();

  this->last_tx_ = f;
  this->last_tx_ms_ = millis();
  char buf[3 * proto::FRAME_LEN + 1];
  format_frame(f, buf);
  ESP_LOGD(TAG, "TX %s (button 0x%02X)", buf, static_cast<uint8_t>(button));
}

// Required by ClimateIR; sends the full current state.
void aux_ir_climate::transmit_state() {
  this->send_(this->mode == climate::CLIMATE_MODE_OFF ? proto::Button::BTN_POWER : proto::Button::BTN_MODE);
}

// ---------------------------------------------------------------- Home Assistant commands
void aux_ir_climate::control(const climate::ClimateCall &call) {
  const climate::ClimateMode old_mode = this->mode;
  const float old_target = this->target_temperature;
  const auto old_fan = this->fan_mode;
  const climate::ClimateSwingMode old_swing = this->swing_mode;

  const bool has_target = call.get_target_temperature().has_value();
  const bool has_fan = call.get_fan_mode().has_value();
  const float requested = has_target ? *call.get_target_temperature() : this->target_temperature;

  this->remember_mode_();
  if (call.get_mode().has_value())
    this->mode = *call.get_mode();
  if (this->mode != old_mode && this->mode != climate::CLIMATE_MODE_OFF &&
      this->proto_mode_(this->mode) != this->proto_mode_(old_mode == climate::CLIMATE_MODE_OFF ? this->last_on_mode_
                                                                                               : old_mode))
    this->recall_mode_(has_target, has_fan);
  if (has_target)
    this->target_temperature = requested;
  if (has_fan)
    this->fan_mode = call.get_fan_mode();
  if (call.get_swing_mode().has_value())
    this->swing_mode = *call.get_swing_mode();

  this->normalize_target_();
  const bool was_on = old_mode != climate::CLIMATE_MODE_OFF;
  const bool is_on = this->mode != climate::CLIMATE_MODE_OFF;

  proto::Button button;
  bool transmit = true;
  if (was_on != is_on) {
    button = proto::Button::BTN_POWER;
    if (!is_on) {
      this->last_on_mode_ = old_mode;
      this->features_[idx_(Feature::FEATURE_HEALTH)] = false;  // the remote drops Health on power off
    } else {
      this->iclean_ = false;
    }
  } else if (!is_on) {
    transmit = false;  // remote sends nothing while off; settings apply at next power on
  } else if (this->mode != old_mode) {
    button = proto::Button::BTN_MODE;
  } else if (has_target && std::fabs(requested - old_target) > 0.01f) {
    // direction of the request; at a limit the remote still sends Temp ▲/▼ with an unchanged setpoint
    button = requested > old_target ? proto::Button::BTN_TEMP_UP : proto::Button::BTN_TEMP_DOWN;
  } else if (this->fan_mode != old_fan) {
    button = proto::Button::BTN_FAN;
    this->features_[idx_(Feature::FEATURE_TURBO)] = false;  // FAN cancels Turbo on the remote
  } else if (this->swing_mode != old_swing) {
    const bool old_v = old_swing == climate::CLIMATE_SWING_VERTICAL || old_swing == climate::CLIMATE_SWING_BOTH;
    const bool new_v =
        this->swing_mode == climate::CLIMATE_SWING_VERTICAL || this->swing_mode == climate::CLIMATE_SWING_BOTH;
    button = old_v != new_v ? proto::Button::BTN_VSWING : proto::Button::BTN_HSWING;
  } else {
    button = proto::Button::BTN_MODE;  // no change: resend the full state
  }

  this->enforce_mode_rules_();
  this->remember_mode_();
  if (transmit)
    this->send_(button);
  this->publish_state();
  this->publish_extras_();
}

void aux_ir_climate::set_feature(Feature feature, bool on) {
  const size_t i = idx_(feature);
  if (feature == Feature::FEATURE_FAHRENHEIT) {
    if (this->features_[i] != on) {
      this->features_[i] = on;
      this->normalize_target_();
      this->send_(proto::Button::BTN_UNITS);  // the remote sends this even while off
      this->publish_state();
    }
    this->publish_extras_();
    return;
  }

  const proto::Mode m = this->proto_mode_(this->mode);
  const char *reason = nullptr;
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    reason = "unit is off";
  } else if (feature == Feature::FEATURE_TURBO && m != proto::Mode::MODE_COOL && m != proto::Mode::MODE_HEAT) {
    reason = "only in Cool/Heat";
  } else if (feature == Feature::FEATURE_ECO && m != proto::Mode::MODE_COOL) {
    reason = "only in Cool";
  } else if (feature == Feature::FEATURE_SLEEP && m == proto::Mode::MODE_FAN) {
    reason = "not in Fan mode";
  }
  if (on && reason != nullptr) {
    ESP_LOGW(TAG, "%s rejected: %s", FEATURE_NAMES[i], reason);
  } else if (this->features_[i] != on && this->mode != climate::CLIMATE_MODE_OFF) {
    this->features_[i] = on;
    static constexpr proto::Button BUTTONS[] = {proto::Button::BTN_TURBO, proto::Button::BTN_ECO, proto::Button::BTN_SLEEP,
                                                proto::Button::BTN_HEALTH};
    this->send_(BUTTONS[i]);
  }
  this->publish_extras_();  // reflect the real state (reverts a rejected switch)
}

void aux_ir_climate::press_button(ButtonType type) {
  const bool on = this->mode != climate::CLIMATE_MODE_OFF;
  if (type == ButtonType::BUTTON_DISPLAY) {
    if (!on) {
      ESP_LOGW(TAG, "Display rejected: unit is off");
      return;
    }
    this->send_(proto::Button::BTN_DISPLAY);
    return;
  }
  // iClean: the Health button pressed while the unit is off
  if (on) {
    ESP_LOGW(TAG, "iClean rejected: turn the unit off first");
    return;
  }
  this->iclean_ = true;
  this->send_(proto::Button::BTN_ICLEAN);
}

// ---------------------------------------------------------------- timer
float aux_ir_climate::quantize_timer_(float hours, bool round_up) {
  if (!(hours > 0.0f))
    return 0.0f;
  const float eps = 1e-4f;
  float h = hours < 10.0f ? (round_up ? std::ceil(hours * 2.0f - eps) : std::round(hours * 2.0f)) / 2.0f
                          : (round_up ? std::ceil(hours - eps) : std::round(hours));
  return clamp(h, 0.5f, 24.0f);
}

float aux_ir_climate::timer_remaining_() const {
  if (!this->timer_active_)
    return 0.0f;
  const int32_t ms = static_cast<int32_t>(this->timer_deadline_ms_ - millis());
  return ms > 0 ? ms / static_cast<float>(MS_PER_HOUR) : 0.0f;
}

void aux_ir_climate::start_timer_(float hours, bool on_timer) {
  const uint32_t ms = static_cast<uint32_t>(hours * MS_PER_HOUR);
  this->timer_active_ = true;
  this->timer_is_on_timer_ = on_timer;
  this->timer_deadline_ms_ = millis() + ms;
  this->set_timeout(TIMER_ID, ms, [this]() { this->on_timer_expired_(); });
  this->set_interval(TIMER_TICK_ID, 60000, [this]() { this->publish_extras_(); });  // count down in HA
}

void aux_ir_climate::clear_timer_() {
  this->timer_active_ = false;
  this->cancel_timeout(TIMER_ID);
  this->cancel_interval(TIMER_TICK_ID);
}

void aux_ir_climate::set_timer(float hours) {
  const float h = quantize_timer_(hours, false);
  if (h <= 0.0f) {
    if (this->timer_active_) {
      this->clear_timer_();
      this->send_(proto::Button::BTN_TIMER);
    }
  } else {
    this->start_timer_(h, this->mode == climate::CLIMATE_MODE_OFF);
    this->send_(proto::Button::BTN_TIMER);
    ESP_LOGI(TAG, "%s timer set: %.1f h", this->timer_is_on_timer_ ? "On" : "Off", h);
  }
  this->publish_extras_();
}

// Mirror what the unit does when its timer fires (no transmission).
void aux_ir_climate::on_timer_expired_() {
  const bool on_timer = this->timer_is_on_timer_;
  this->clear_timer_();
  if (on_timer) {
    if (this->mode == climate::CLIMATE_MODE_OFF)
      this->mode = this->last_on_mode_;
  } else if (this->mode != climate::CLIMATE_MODE_OFF) {
    this->last_on_mode_ = this->mode;
    this->mode = climate::CLIMATE_MODE_OFF;
    this->features_[idx_(Feature::FEATURE_HEALTH)] = false;
  }
  ESP_LOGI(TAG, "%s timer expired", on_timer ? "On" : "Off");
  this->publish_state();
  this->publish_extras_();
}

// ---------------------------------------------------------------- receive (original remote)
bool aux_ir_climate::on_receive(remote_base::RemoteReceiveData data) {
  if (!data.expect_item(proto::HEADER_MARK, proto::HEADER_SPACE))
    return false;
  proto::Frame f{};
  for (size_t i = 0; i < proto::FRAME_LEN; i++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (data.expect_item(proto::BIT_MARK, proto::ONE_SPACE)) {
        f[i] |= 1 << bit;
      } else if (!data.expect_item(proto::BIT_MARK, proto::ZERO_SPACE)) {
        return false;
      }
    }
  }

  char buf[3 * proto::FRAME_LEN + 1];
  format_frame(f, buf);
  proto::State s;
  proto::Button button;
  if (!proto::decode(f, s, button)) {
    ESP_LOGW(TAG, "RX invalid frame (checksum/header): %s", buf);
    return false;
  }
  if (f == this->last_tx_ && millis() - this->last_tx_ms_ < ECHO_WINDOW_MS) {
    ESP_LOGV(TAG, "RX echo of own transmission ignored");
    return true;
  }
  ESP_LOGD(TAG, "RX %s (button 0x%02X)", buf, static_cast<uint8_t>(button));
  this->apply_received_(s);
  return true;
}

void aux_ir_climate::apply_received_(const proto::State &s) {
  this->iclean_ = s.iclean;
  this->features_[idx_(Feature::FEATURE_FAHRENHEIT)] = s.fahrenheit;

  if (s.power) {
    this->mode = this->climate_mode_(s.mode);
    this->last_on_mode_ = this->mode;
  } else {
    if (!s.iclean)
      this->last_on_mode_ = this->climate_mode_(s.mode);
    this->mode = climate::CLIMATE_MODE_OFF;
  }

  if (s.fahrenheit && s.temp_f != 0) {
    this->target_temperature = (s.temp_f - 32) * 5.0f / 9.0f;  // exact °F from byte 10
  } else if (s.temp_c != 0) {
    this->target_temperature = s.temp_c;
  }  // FAN / AUTO carry no setpoint: keep the last one
  this->normalize_target_();

  if (!s.turbo && !s.iclean) {  // Turbo and iClean force High; keep the user's fan
    switch (s.fan) {
      case proto::Fan::FAN_LOW:
        this->fan_mode = climate::CLIMATE_FAN_LOW;
        break;
      case proto::Fan::FAN_MEDIUM:
        this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
        break;
      case proto::Fan::FAN_HIGH:
        this->fan_mode = climate::CLIMATE_FAN_HIGH;
        break;
      default:
        this->fan_mode = climate::CLIMATE_FAN_AUTO;
        break;
    }
  }
  this->swing_mode = s.vswing && s.hswing ? climate::CLIMATE_SWING_BOTH
                     : s.vswing           ? climate::CLIMATE_SWING_VERTICAL
                     : s.hswing           ? climate::CLIMATE_SWING_HORIZONTAL
                                          : climate::CLIMATE_SWING_OFF;

  this->features_[idx_(Feature::FEATURE_TURBO)] = s.turbo;
  this->features_[idx_(Feature::FEATURE_ECO)] = s.eco;
  this->features_[idx_(Feature::FEATURE_SLEEP)] = s.sleep;
  this->features_[idx_(Feature::FEATURE_HEALTH)] = s.health;

  this->remember_mode_();

  if (s.on_timer || s.off_timer) {
    const float h = s.timer_hours + s.timer_minutes / 60.0f;
    if (h > 0.0f)
      this->start_timer_(h, s.on_timer);
  } else if (this->timer_active_) {
    this->clear_timer_();
  }

  this->publish_state();
  this->publish_extras_();
}

void aux_ir_climate::publish_extras_() {
#ifdef USE_SWITCH
  for (size_t i = 0; i < idx_(Feature::FEATURE_COUNT); i++) {
    if (this->switches_[i] != nullptr)
      this->switches_[i]->publish_state(this->features_[i]);
  }
#endif
#ifdef USE_NUMBER
  if (this->timer_number_ != nullptr)
    this->timer_number_->publish_state(this->timer_active_ ? quantize_timer_(this->timer_remaining_(), true) : 0.0f);
#endif
}

}  // namespace esphome::aux_ir