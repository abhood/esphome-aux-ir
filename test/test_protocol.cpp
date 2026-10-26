#include "../components/aux12/aux12_protocol.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using namespace esphome::aux12::proto;

static std::string hex(const Frame &f) {
  std::string s; char b[4];
  for (auto x : f) { snprintf(b, sizeof b, "%02X ", x); s += b; }
  s.pop_back(); return s;
}
int main() {
  std::ifstream in("frames.txt"); std::string line; int n = 0, fail = 0, fcheck = 0, fbad = 0;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream is(line); Frame f{}; unsigned v; size_t i = 0;
    while (is >> std::hex >> v && i < FRAME_LEN) f[i++] = v;
    State s; Button b; n++;
    if (!decode(f, s, b)) { printf("REJECTED  %s\n", line.c_str()); fail++; continue; }
    Frame g = encode(s, b);
    if (g != f) { printf("MISMATCH  %s\n       -> %s\n", line.c_str(), hex(g).c_str()); fail++; }
    if (s.fahrenheit && s.temp_f) {        // remote's °F -> °C rounding must match ours
      fcheck++;
      if (f_to_c(s.temp_f) != s.temp_c) { fbad++; printf("F->C %d F: frame %d C, ours %d C\n", s.temp_f, s.temp_c, f_to_c(s.temp_f)); }
    }
  }
  // corrupted frames must be rejected
  Frame ok = encode(State{}, Button::POWER); int rej = 0;
  for (size_t i = 0; i < FRAME_LEN; i++) for (int bit = 0; bit < 8; bit++) {
    Frame c = ok; c[i] ^= 1 << bit; State s; Button b; if (!decode(c, s, b)) rej++;
  }
  // full range encode/decode
  int range_bad = 0;
  for (int c = C_MIN; c <= C_MAX; c++) { State s; s.temp_c = c; State d; Button b; decode(encode(s, Button::TEMP_UP), d, b); range_bad += d.temp_c != c; }
  for (int F = F_MIN; F <= F_MAX; F++) { State s; s.fahrenheit = true; s.temp_f = F; s.temp_c = f_to_c(F); State d; Button b;
    decode(encode(s, Button::TEMP_UP), d, b); range_bad += d.temp_f != F || d.temp_c != f_to_c(F); }
  for (int c = C_MIN; c <= C_MAX; c++) { int F = c_to_f(c); range_bad += F < F_MIN || F > F_MAX || f_to_c(F) != c; }
  printf("frames %d, round-trip failures %d | °F frames %d, F->C mismatches %d | single-bit corruptions rejected %d/104 | range errors %d\n",
         n, fail, fcheck, fbad, rej, range_bad);
  return fail || fbad || range_bad;
}
