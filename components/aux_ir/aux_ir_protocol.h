#pragma once
// AUX 13-byte IR protocol (YKR-H/101E-style remote), decoded from captured remote frames.
// Pure C++ with no ESPHome dependencies, so it can be unit-tested off-target.
//
//  byte  bits   field
//   0    all    0xC3 header
//   1    7..3   setpoint °C - 8 (0 in FAN/AUTO)        2..0  vertical vane (7 fixed, 0 swing)
//   2    7..5   horizontal vane (7 fixed, 0 swing)
//   4    7..5   fan (A0 auto, 60 low, 40 med, 20 high) 4..0  timer hours
//   5    6      turbo                                  5..0  timer minutes
//   6    7..5   mode   4 health   3 iFeel   2 sleep   1 °F display
//   7,8         iFeel sensor bytes (unused here, carried through)
//   9    7 on-timer  6 off-timer  5 power  4 aux heat  3 eco  2 iClean  1 health
//  10    7      °F setpoint valid, 6..1 = °F - 56
//  11    all    button code
//  12    all    sum of bytes 0..11
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace esphome::aux_ir {

namespace proto {

static constexpr size_t FRAME_LEN = 13;
using Frame = std::array<uint8_t, FRAME_LEN>;

// IR timing (µs)
static constexpr uint32_t CARRIER_HZ = 38000;
static constexpr uint32_t HEADER_MARK = 9000;
static constexpr uint32_t HEADER_SPACE = 4500;
static constexpr uint32_t BIT_MARK = 560;
static constexpr uint32_t ZERO_SPACE = 580;
static constexpr uint32_t ONE_SPACE = 1690;

static constexpr uint8_t FRAME_HEADER = 0xC3;

static constexpr int C_MIN = 16, C_MAX = 32;
static constexpr int F_MIN = 60, F_MAX = 90;

enum class Mode : uint8_t { MODE_AUTO = 0x00, MODE_COOL = 0x20, MODE_DRY = 0x40, MODE_HEAT = 0x80, MODE_FAN = 0xC0 };
enum class Fan : uint8_t { FAN_AUTO = 0xA0, FAN_LOW = 0x60, FAN_MEDIUM = 0x40, FAN_HIGH = 0x20 };

enum class Button : uint8_t {
  BTN_TEMP_UP = 0x00,
  BTN_TEMP_DOWN = 0x01,
  BTN_VSWING = 0x02,
  BTN_HSWING = 0x03,
  BTN_FAN = 0x04,
  BTN_POWER = 0x05,
  BTN_MODE = 0x06,
  BTN_HEALTH = 0x07,
  BTN_TURBO = 0x08,
  BTN_SLEEP = 0x0B,
  BTN_TIMER = 0x0D,
  BTN_ECO = 0x13,
  BTN_DISPLAY = 0x15,
  BTN_AUX_HEAT = 0x16,  // ECO button while in HEAT
  BTN_UNITS = 0x17,
  BTN_ICLEAN = 0x19,    // HEALTH button while OFF
  BTN_IFEEL = 0x1E,
};

// Byte 6
static constexpr uint8_t B6_MODE_MASK = 0xE0;
static constexpr uint8_t B6_HEALTH = 0x10;
static constexpr uint8_t B6_IFEEL = 0x08;
static constexpr uint8_t B6_SLEEP = 0x04;
static constexpr uint8_t B6_FAHRENHEIT = 0x02;
// Byte 9
static constexpr uint8_t B9_ON_TIMER = 0x80;
static constexpr uint8_t B9_OFF_TIMER = 0x40;
static constexpr uint8_t B9_POWER = 0x20;
static constexpr uint8_t B9_AUX_HEAT = 0x10;
static constexpr uint8_t B9_ECO = 0x08;
static constexpr uint8_t B9_ICLEAN = 0x04;
static constexpr uint8_t B9_HEALTH = 0x02;
// Other
static constexpr uint8_t B5_TURBO = 0x40;
static constexpr uint8_t B10_F_VALID = 0x80;
static constexpr uint8_t VANE_V_FIXED = 0x07;
static constexpr uint8_t VANE_H_FIXED = 0xE0;

struct State {
  bool power{true};
  Mode mode{Mode::MODE_COOL};
  uint8_t temp_c{24};      // 16..32, 0 = not sent (FAN / AUTO)
  bool fahrenheit{false};  // °F display
  uint8_t temp_f{0};       // 60..90, 0 = not sent
  Fan fan{Fan::FAN_AUTO};
  bool vswing{false};
  bool hswing{false};
  bool turbo{false};
  bool eco{false};
  bool sleep{false};
  bool health{false};
  bool aux_heat{false};
  bool iclean{false};
  bool ifeel{false};
  uint8_t ifeel_b7{0};
  uint8_t ifeel_b8{0};
  bool on_timer{false};
  bool off_timer{false};
  uint8_t timer_hours{0};    // 0..24
  uint8_t timer_minutes{0};  // 0..59 (remote uses 0 / 30)
};

inline bool mode_has_setpoint(Mode m) { return m == Mode::MODE_COOL || m == Mode::MODE_DRY || m == Mode::MODE_HEAT; }

inline int c_to_f(int c) { return static_cast<int>(std::lround(c * 9.0f / 5.0f + 32.0f)); }
inline int f_to_c(int f) { return static_cast<int>(std::lround((f - 32) * 5.0f / 9.0f)); }

inline uint8_t checksum(const Frame &f) {
  uint8_t s = 0;
  for (size_t i = 0; i < FRAME_LEN - 1; i++)
    s += f[i];
  return s;
}

inline bool valid(const Frame &f) { return f[0] == FRAME_HEADER && f[FRAME_LEN - 1] == checksum(f); }

inline Frame encode(const State &s, Button button) {
  Frame f{};
  f[0] = FRAME_HEADER;

  uint8_t c = s.temp_c;
  if (!mode_has_setpoint(s.mode) || c < C_MIN || c > C_MAX)
    c = 0;
  f[1] = static_cast<uint8_t>((c ? (c - 8) << 3 : 0) | (s.vswing ? 0 : VANE_V_FIXED));
  f[2] = s.hswing ? 0 : VANE_H_FIXED;

  f[4] = static_cast<uint8_t>(s.turbo ? Fan::FAN_HIGH : s.fan) | (s.timer_hours & 0x1F);
  f[5] = (s.turbo ? B5_TURBO : 0) | (s.timer_minutes & 0x3F);

  f[6] = static_cast<uint8_t>(s.mode);
  if (s.health)
    f[6] |= B6_HEALTH;
  if (s.ifeel)
    f[6] |= B6_IFEEL;
  if (s.sleep)
    f[6] |= B6_SLEEP;
  if (s.fahrenheit)
    f[6] |= B6_FAHRENHEIT;

  if (s.ifeel) {
    f[7] = s.ifeel_b7;
    f[8] = s.ifeel_b8;
  }

  if (s.on_timer)
    f[9] |= B9_ON_TIMER;
  if (s.off_timer)
    f[9] |= B9_OFF_TIMER;
  if (s.power)
    f[9] |= B9_POWER;
  if (s.aux_heat)
    f[9] |= B9_AUX_HEAT;
  if (s.eco)
    f[9] |= B9_ECO;
  if (s.iclean)
    f[9] |= B9_ICLEAN;
  if (s.health)
    f[9] |= B9_HEALTH;

  if (s.fahrenheit && c && s.temp_f >= F_MIN && s.temp_f <= F_MAX)
    f[10] = B10_F_VALID | static_cast<uint8_t>((s.temp_f - 56) << 1);

  f[11] = static_cast<uint8_t>(button);
  f[12] = checksum(f);
  return f;
}

// Returns false for anything that isn't a valid AUX frame.
inline bool decode(const Frame &f, State &s, Button &button) {
  if (!valid(f))
    return false;
  const uint8_t mode = f[6] & B6_MODE_MASK;
  if (mode != 0x00 && mode != 0x20 && mode != 0x40 && mode != 0x80 && mode != 0xC0)
    return false;
  const uint8_t fan = f[4] & 0xE0;
  if (fan != 0xA0 && fan != 0x60 && fan != 0x40 && fan != 0x20)
    return false;

  s = State{};
  s.mode = static_cast<Mode>(mode);
  s.fan = static_cast<Fan>(fan);
  const uint8_t tc = f[1] >> 3;
  s.temp_c = tc ? tc + 8 : 0;
  s.vswing = (f[1] & 0x07) == 0;
  s.hswing = f[2] == 0;
  s.timer_hours = f[4] & 0x1F;
  s.timer_minutes = f[5] & 0x3F;
  s.turbo = f[5] & B5_TURBO;
  s.health = f[6] & B6_HEALTH;
  s.ifeel = f[6] & B6_IFEEL;
  s.sleep = f[6] & B6_SLEEP;
  s.fahrenheit = f[6] & B6_FAHRENHEIT;
  s.ifeel_b7 = f[7];
  s.ifeel_b8 = f[8];
  s.on_timer = f[9] & B9_ON_TIMER;
  s.off_timer = f[9] & B9_OFF_TIMER;
  s.power = f[9] & B9_POWER;
  s.aux_heat = f[9] & B9_AUX_HEAT;
  s.eco = f[9] & B9_ECO;
  s.iclean = f[9] & B9_ICLEAN;
  s.temp_f = (f[10] & B10_F_VALID) ? ((f[10] & 0x7F) >> 1) + 56 : 0;
  button = static_cast<Button>(f[11]);
  return true;
}

}  // namespace proto
}  // namespace esphome::aux_ir