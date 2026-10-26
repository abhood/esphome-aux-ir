// Host test for the aux12 component, built against the ESPHome 2026.10 sources.
// Replaces the generated main.cpp. Drives the REAL Aux12Climate:
//   1. Replays every frame from capture session 2 (real IR timings) into on_receive().
//   2. For every remote button press in that session, syncs to the frame before it, performs the
//      equivalent Home Assistant action, and compares the transmitted frame to what the remote sent.
//   3. Targeted checks: echo suppression, °F exactness, rejected features, timer, iClean.
#include "esphome.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace esphome;
using namespace esphome::aux12;

struct FakeTx : public remote_base::RemoteTransmitterBase {
  FakeTx() : RemoteTransmitterBase(nullptr) {}
  std::vector<int32_t> last;
  uint32_t carrier{0};
  int count{0};
  void send_internal(uint32_t, uint32_t) override {
    last = this->temp_.get_data();
    carrier = this->temp_.get_carrier_frequency();
    count++;
  }
};

static FakeTx tx;
static Aux12Climate *ac;
static Aux12Switch *sw[5];
static Aux12Button *btn_display, *btn_iclean;
static Aux12TimerNumber *timer_num;
static int failures = 0;

#define CHECK(cond, ...)                \
  do {                                  \
    if (!(cond)) {                      \
      failures++;                       \
      printf("  FAIL %s:%d ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);              \
      printf("\n");                     \
    }                                   \
  } while (0)

static std::string hex(const proto::Frame &f) {
  std::string s;
  char b[4];
  for (auto x : f) {
    snprintf(b, sizeof b, "%02X ", x);
    s += b;
  }
  s.pop_back();
  return s;
}
static proto::Frame unhex(const std::string &h) {
  proto::Frame f{};
  for (size_t i = 0; i < proto::FRAME_LEN; i++)
    f[i] = std::stoul(h.substr(2 * i, 2), nullptr, 16);
  return f;
}
static std::vector<int32_t> timings(const proto::Frame &f) {
  std::vector<int32_t> t{9000, -4500};
  for (auto b : f)
    for (int k = 0; k < 8; k++) {
      t.push_back(560);
      t.push_back((b >> k) & 1 ? -1690 : -580);
    }
  t.push_back(560);
  return t;
}
// Decode what the fake transmitter sent (exact timings, so tolerance 0 would do; reuse the receiver path).
static bool tx_frame(proto::Frame &out) {
  if (tx.last.size() < 2 + 104 * 2)
    return false;
  if (tx.last[0] != (int32_t) proto::HEADER_MARK || tx.last[1] != -(int32_t) proto::HEADER_SPACE)
    return false;
  out.fill(0);
  for (size_t i = 0; i < 104; i++) {
    if (tx.last[2 + 2 * i] != (int32_t) proto::BIT_MARK)
      return false;
    if (tx.last[3 + 2 * i] == -(int32_t) proto::ONE_SPACE)
      out[i / 8] |= 1 << (i % 8);
  }
  return true;
}
static bool rx(const std::vector<int32_t> &raw) {
  remote_base::RemoteReceiveData data(raw, 30, remote_base::TOLERANCE_MODE_PERCENTAGE);
  return static_cast<remote_base::RemoteReceiverListener *>(ac)->on_receive(data);
}
static void clear_echo() { tx.last.clear(); ac->set_transmitter(&tx); }

static int unit_sp() {  // setpoint in the display unit
  if (sw[4]->state)
    return (int) lroundf(ac->target_temperature * 9 / 5 + 32);
  return (int) lroundf(ac->target_temperature);
}
static float c_of(int sp) { return sw[4]->state ? (sp - 32) * 5.0f / 9.0f : (float) sp; }

static const climate::ClimateMode CYCLE[] = {climate::CLIMATE_MODE_COOL, climate::CLIMATE_MODE_DRY,
                                             climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY,
                                             climate::CLIMATE_MODE_HEAT_COOL};

static climate::ClimateMode mode_of(const proto::Frame &f) {
  switch (f[6] & 0xE0) {
    case 0x20: return climate::CLIMATE_MODE_COOL;
    case 0x40: return climate::CLIMATE_MODE_DRY;
    case 0x80: return climate::CLIMATE_MODE_HEAT;
    case 0xC0: return climate::CLIMATE_MODE_FAN_ONLY;
    default: return climate::CLIMATE_MODE_HEAT_COOL;
  }
}

// Perform the Home Assistant action equivalent to a remote button. Returns false if not mappable.
static bool do_action(const std::string &a, const proto::Frame &expect) {
  if (a == "temp_up" || a == "temp_down") {
    int sp = unit_sp() + (a == "temp_up" ? 1 : -1);
    ac->make_call().set_target_temperature(c_of(sp)).perform();
  } else if (a == "mode") {
    size_t i = 0;
    while (i < 5 && CYCLE[i] != ac->mode) i++;
    ac->make_call().set_mode(CYCLE[(i + 1) % 5]).perform();
  } else if (a == "fan") {
    auto f = ac->fan_mode.value_or(climate::CLIMATE_FAN_AUTO);
    climate::ClimateFanMode next;
    if (ac->mode == climate::CLIMATE_MODE_FAN_ONLY)
      next = f == climate::CLIMATE_FAN_LOW ? climate::CLIMATE_FAN_MEDIUM
             : f == climate::CLIMATE_FAN_MEDIUM ? climate::CLIMATE_FAN_HIGH : climate::CLIMATE_FAN_LOW;
    else
      next = f == climate::CLIMATE_FAN_AUTO ? climate::CLIMATE_FAN_LOW
             : f == climate::CLIMATE_FAN_LOW ? climate::CLIMATE_FAN_MEDIUM
             : f == climate::CLIMATE_FAN_MEDIUM ? climate::CLIMATE_FAN_HIGH : climate::CLIMATE_FAN_AUTO;
    ac->make_call().set_fan_mode(next).perform();
  } else if (a == "vswing" || a == "hswing") {
    bool v = ac->swing_mode == climate::CLIMATE_SWING_VERTICAL || ac->swing_mode == climate::CLIMATE_SWING_BOTH;
    bool h = ac->swing_mode == climate::CLIMATE_SWING_HORIZONTAL || ac->swing_mode == climate::CLIMATE_SWING_BOTH;
    (a == "vswing" ? v : h) ^= true;
    ac->make_call()
        .set_swing_mode(v && h ? climate::CLIMATE_SWING_BOTH
                        : v    ? climate::CLIMATE_SWING_VERTICAL
                        : h    ? climate::CLIMATE_SWING_HORIZONTAL : climate::CLIMATE_SWING_OFF)
        .perform();
  } else if (a == "turbo" || a == "eco" || a == "sleep" || a == "units" || a == "clean") {
    if (a == "clean" && ac->mode == climate::CLIMATE_MODE_OFF) {
      btn_iclean->press();
      return true;
    }
    int i = a == "turbo" ? 0 : a == "eco" ? 1 : a == "sleep" ? 2 : a == "clean" ? 3 : 4;
    if (a == "eco" && ac->mode == climate::CLIMATE_MODE_HEAT)
      return false;  // aux-heat toggle: intentionally not exposed
    sw[i]->state ? sw[i]->turn_off() : sw[i]->turn_on();
  } else if (a == "display") {
    btn_display->press();
  } else if (a == "power") {
    ac->make_call().set_mode(ac->mode == climate::CLIMATE_MODE_OFF ? mode_of(expect) : climate::CLIMATE_MODE_OFF).perform();
  } else if (a == "manual") {
    if (expect[11] != 0x0D)
      return false;  // only timer steps are actions
    float h = (expect[9] & 0xC0) ? (expect[4] & 0x1F) + (expect[5] & 0x3F) / 60.0f : 0.0f;
    auto call = timer_num->make_call();
    call.set_value(h);
    call.perform();
  } else {
    return false;  // ifeel: not supported
  }
  return true;
}

static void build() {
  ac = new Aux12Climate();
  ac->set_transmitter(&tx);
  ac->set_use_fahrenheit(false);
  App.register_climate(ac, "AUX Heat Pump", 1001UL, 0);
  const Feature feats[] = {Feature::TURBO, Feature::ECO, Feature::SLEEP, Feature::HEALTH, Feature::FAHRENHEIT};
  for (int i = 0; i < 5; i++) {
    static const char *const names[] = {"Turbo", "Eco", "Sleep", "Health", "F"};
    sw[i] = new Aux12Switch(feats[i]);
    App.register_switch(sw[i], names[i], 2000UL + i, 0);
    sw[i]->set_parent(ac);
    ac->set_feature_switch(feats[i], sw[i]);
  }
  btn_display = new Aux12Button(ButtonType::DISPLAY);
  App.register_button(btn_display, "Display", 3001UL, 0);
  btn_display->set_parent(ac);
  btn_iclean = new Aux12Button(ButtonType::ICLEAN);
  App.register_button(btn_iclean, "iClean", 3002UL, 0);
  btn_iclean->set_parent(ac);
  timer_num = new Aux12TimerNumber();
  App.register_number(timer_num, "Timer", 4001UL, 0);
  timer_num->set_parent(ac);
  timer_num->traits.set_min_value(0);
  timer_num->traits.set_max_value(24);
  timer_num->traits.set_step(0.5);
  ac->set_timer_number(timer_num);
}

static void test_replay_session() {
  printf("\n[1] Session 2 replay: receive every frame, then mirror every button press\n");
  std::ifstream in("session2_steps.txt");
  std::string line, prev_hex;
  int rx_ok = 0, rx_n = 0, tx_n = 0, tx_exact = 0, skipped = 0, known = 0;
  std::vector<int32_t> prev_raw;
  while (std::getline(in, line)) {
    std::istringstream is(line);
    int seq, emitted, nraw;
    std::string action, h;
    is >> seq >> action >> emitted >> h >> nraw;
    if (!emitted)
      continue;
    std::vector<int32_t> raw(nraw);
    for (auto &v : raw) is >> v;
    proto::Frame expect = unhex(h);
    if (raw.empty())
      raw = timings(expect);

    // Not comparable: iFeel on (sensor bytes we never send), and seq 6 where the remote recalls a Heat
    // setpoint it remembered from before the capture started.
    const bool ifeel = (expect[6] & 0x08) || (unhex(prev_hex.empty() ? h : prev_hex)[6] & 0x08) || action == "ifeel";
    if (seq == 6 || ifeel) {
      if (!prev_hex.empty())
        known++;
    } else if (!prev_hex.empty()) {
      // sync to the previous remote frame, then act like Home Assistant
      clear_echo();
      rx(prev_raw);
      int before = tx.count;
      if (do_action(action, expect)) {
        proto::Frame got;
        if (tx.count == before || !tx_frame(got)) {
          printf("  seq %3d %-9s NO TX (remote sent %s)\n", seq, action.c_str(), hex(expect).c_str());
          failures++;
        } else {
          tx_n++;
          if (got == expect) {
            tx_exact++;
          } else {
            printf("  seq %3d %-9s remote %s\n                    ours   %s\n", seq, action.c_str(),
                   hex(expect).c_str(), hex(got).c_str());
            failures++;
          }
        }
      } else {
        skipped++;
      }
    }
    // receive the remote's frame
    clear_echo();
    rx_n++;
    if (rx(raw))
      rx_ok++;
    else {
      printf("  seq %3d RX rejected %s\n", seq, h.c_str());
      failures++;
    }
    prev_hex = h;
    prev_raw = raw;
  }
  printf("  RX: %d/%d frames accepted (real recorded timings)\n", rx_ok, rx_n);
  printf("  TX: %d/%d HA actions produced the remote's exact frame\n", tx_exact, tx_n);
  printf("      not compared: %d not mappable (Eco-in-Heat = aux heat), %d known (iFeel steps, pre-capture memory)\n",
         skipped, known);
}

static void test_targeted() {
  printf("\n[2] Targeted checks\n");
  // Fahrenheit: every °F 60..90 must round-trip exactly
  clear_echo();
  ac->make_call().set_mode(climate::CLIMATE_MODE_COOL).perform();
  {
    proto::State s;
    s.fahrenheit = true;
    int bad = 0;
    for (int F = proto::F_MIN; F <= proto::F_MAX; F++) {
      s.temp_f = F;
      s.temp_c = proto::f_to_c(F);
      clear_echo();
      rx(timings(proto::encode(s, proto::Button::TEMP_UP)));
      if (!sw[4]->state || unit_sp() != F)
        bad++;
      // and HA setting that °F transmits exactly that °F
      ac->make_call().set_target_temperature((F - 32) * 5.0f / 9.0f).perform();
      proto::Frame got;
      tx_frame(got);
      proto::State d;
      proto::Button b;
      if (!proto::decode(got, d, b) || d.temp_f != F || d.temp_c != proto::f_to_c(F))
        bad++;
    }
    CHECK(bad == 0, "°F round trip errors: %d", bad);
    printf("  °F 60–90: RX → HA and HA → TX exact: %s\n", bad ? "NO" : "yes");
  }
  // Clamp: HA asks for 95 °F
  {
    ac->make_call().set_target_temperature((95 - 32) * 5.0f / 9.0f).perform();
    proto::Frame got;
    tx_frame(got);
    CHECK(((got[10] & 0x7F) >> 1) + 56 == 90, "95 °F not clamped to 90");
  }
  // Echo suppression
  {
    clear_echo();
    ac->make_call().set_mode(climate::CLIMATE_MODE_COOL).set_target_temperature(c_of(76)).perform();
    proto::Frame sent;
    tx_frame(sent);
    auto before_mode = ac->mode;
    bool consumed = rx(timings(sent));
    CHECK(consumed && ac->mode == before_mode, "echo not consumed");
    printf("  own-transmission echo ignored: %s\n", consumed ? "yes" : "no");
  }
  // Corrupted frame rejected, state unchanged
  {
    proto::Frame good = proto::encode(proto::State{}, proto::Button::MODE);
    good[3] ^= 0x10;
    float t = ac->target_temperature;
    CHECK(!rx(timings(good)) && ac->target_temperature == t, "corrupted frame accepted");
    printf("  bad-checksum frame rejected: yes\n");
  }
  // Rules: Turbo rejected in Dry, Sleep rejected in Fan, nothing sent while off
  {
    clear_echo();
    ac->make_call().set_mode(climate::CLIMATE_MODE_DRY).perform();
    int n = tx.count;
    sw[0]->turn_on();
    CHECK(!sw[0]->state && tx.count == n, "turbo accepted in dry");
    ac->make_call().set_mode(climate::CLIMATE_MODE_FAN_ONLY).perform();
    CHECK(ac->fan_mode.has_value() && *ac->fan_mode != climate::CLIMATE_FAN_AUTO, "Auto fan in Fan mode");
    n = tx.count;
    sw[2]->turn_on();
    CHECK(!sw[2]->state && tx.count == n, "sleep accepted in fan");
    ac->make_call().set_mode(climate::CLIMATE_MODE_OFF).perform();
    n = tx.count;
    ac->make_call().set_fan_mode(climate::CLIMATE_FAN_LOW).perform();
    sw[3]->turn_on();
    btn_display->press();
    CHECK(tx.count == n && !sw[3]->state, "transmitted while off");
    printf("  Turbo blocked in Dry, Sleep blocked in Fan, no Auto fan in Fan mode, silent while off: yes\n");
  }
  // iClean only while off; frame matches the remote's
  {
    proto::Frame got;
    btn_iclean->press();
    tx_frame(got);
    CHECK((got[9] & 0x24) == 0x04 && got[11] == 0x19 && (got[6] & 0xE0) == 0xC0, "iClean frame %s", hex(got).c_str());
    ac->make_call().set_mode(climate::CLIMATE_MODE_COOL).perform();
    int n = tx.count;
    btn_iclean->press();
    CHECK(tx.count == n, "iClean sent while on");
    printf("  iClean only while off, remote-style frame: yes\n");
  }
  // Timer: 2.5 h off-timer, kept in following frames, cancel, snapping
  {
    proto::Frame got;
    auto set = [](float v) { auto c = timer_num->make_call(); c.set_value(v); c.perform(); };
    set(2.5f);
    tx_frame(got);
    CHECK((got[4] & 0x1F) == 2 && (got[5] & 0x3F) == 30 && (got[9] & 0xC0) == 0x40 && got[11] == 0x0D, "timer %s",
          hex(got).c_str());
    ac->make_call().set_target_temperature(c_of(unit_sp() + 1)).perform();
    tx_frame(got);
    CHECK((got[9] & 0x40) && (got[4] & 0x1F) == 2, "timer dropped from next frame");
    set(12.4f);
    tx_frame(got);
    CHECK((got[4] & 0x1F) == 12 && (got[5] & 0x3F) == 0, "12.4 h not snapped to 12");
    set(0);
    tx_frame(got);
    CHECK((got[9] & 0xC0) == 0 && (got[4] & 0x1F) == 0 && got[11] == 0x0D, "cancel %s", hex(got).c_str());
    CHECK(timer_num->state == 0, "timer number not 0");
    printf("  Timer set / carried / snapped / cancelled: yes\n");
  }
  // Per-mode memory: Cool 20, Heat 28 -> switching modes restores each
  {
    ac->make_call().set_mode(climate::CLIMATE_MODE_COOL).set_target_temperature(c_of(68)).perform();
    ac->make_call().set_mode(climate::CLIMATE_MODE_HEAT).set_target_temperature(c_of(82)).perform();
    ac->make_call().set_mode(climate::CLIMATE_MODE_COOL).perform();
    int cool = unit_sp();
    ac->make_call().set_mode(climate::CLIMATE_MODE_HEAT).perform();
    int heat = unit_sp();
    CHECK(cool == 68 && heat == 82, "mode memory cool=%d heat=%d", cool, heat);
    printf("  Per-mode setpoint memory: yes (Cool %d °F, Heat %d °F)\n", cool, heat);
  }
  // Power off drops Health; carrier frequency
  {
    sw[3]->turn_on();
    CHECK(sw[3]->state, "health not on");
    ac->make_call().set_mode(climate::CLIMATE_MODE_OFF).perform();
    CHECK(!sw[3]->state, "health survived power off");
    CHECK(tx.carrier == 38000, "carrier %u", tx.carrier);
    printf("  Health cleared on power off, 38 kHz carrier: yes\n");
  }
}

void setup() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  // Same start-up sequence as the generated main.cpp
  auto *log = new logger::Logger(115200);
  log->create_pthread_key();
  log->pre_setup();
  log->set_log_level(ESPHOME_LOG_LEVEL_WARN);
  new (&App) Application;
  App.pre_setup("aux12test", 9, "AUX test", 8);
  build();
  App.register_component_(ac, 1);  // as the generated main.cpp does (setup() is a friend)
  ac->setup();
  test_replay_session();
  test_targeted();
  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  fflush(stdout);
  exit(failures ? 1 : 0);
}
void loop() {}
