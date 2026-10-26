# esphome-aux-ir

Native [ESPHome](https://esphome.io) climate component for **AUX-family air conditioners and heat pumps controlled by infrared**.

It transmits the same 13-byte frames as the original handheld remote, and decodes the remote's frames so Home Assistant stays in sync when someone uses the physical remote.

> **Status: beta.** The protocol is fully decoded from about 900 captured remote frames, and the component is tested against ESPHome 2026.10. Field testing on real units is ongoing. Reports from other AUX models are welcome.

## Features

- **Climate entity:** Off, Cool, Heat, Dry, Fan and Auto.
  - Fan speeds: Auto, Low, Medium and High.
  - Swing: Off, Vertical, Horizontal and Both.
- **Exact °F setpoints (60–90 °F).** The remote's whole-°F value is sent and read directly, so 75 °F stays 75 °F instead of drifting through a °C conversion.
- **Remote sync.** With an IR receiver, presses on the handheld remote update Home Assistant: mode, setpoint, fan, swing, features and timer.
- **Remote behaviour reproduced:**
  - Each mode keeps its own setpoint and fan.
  - Every frame carries the code of the button that produced it.
  - Features are only allowed in the modes the remote allows.
- **Optional entities:** switches for Turbo, Eco, Sleep, Health and °F display; buttons for Display and iClean; a number for the Timer.
- **Echo suppression.** The component recognises and ignores its own transmissions.
- **Checksum validation.** Corrupted frames are rejected instead of being applied.
- **No external libraries.** It is built on ESPHome's `climate_ir`, `remote_transmitter` and `remote_receiver`.

## Compatibility

Developed on an AUX indoor unit with a **YKR-H/101E**-style remote that sends 13-byte frames. Other AUX-built units that use the same remote family will likely work. These are sold under several brands, because AUX manufactures for other companies.

**How to check your unit:** add `dump: raw` to your receiver and press a button on the remote. Your remote uses this protocol if:
- the frame starts with a ~9000 µs mark and a ~4500 µs space;
- it is 104 bits long;
- the first byte is `0xC3`.

Requires **ESPHome 2026.10** or newer.

| | |
|---|---|
| Tested hardware | Seeed Studio XIAO Smart IR Mate (ESP32-C3) |
| Should work on | Any ESP32 / ESP8266 with an IR LED and an IR receiver |

Not to be confused with [`aux_ac`](https://github.com/GrKoR/esphome_aux_ac_component), which controls AUX units over UART through the Wi-Fi module port. This component uses IR only and doesn't require opening the unit.

## Installation

```yaml
external_components:
  - source: github://YOUR_USERNAME/esphome-aux-ir@main
    components: [aux_ir]
```

## Configuration

Minimal:

```yaml
remote_transmitter:
  id: ir_tx
  pin: GPIOXX
  carrier_duty_percent: 50%

climate:
  - platform: aux_ir
    name: "Heat Pump"
    transmitter_id: ir_tx
```

Full, with remote sync and all optional entities:

```yaml
remote_transmitter:
  id: ir_tx
  pin: GPIOXX
  carrier_duty_percent: 50%

remote_receiver:
  id: ir_rx
  pin:
    number: GPIOXX
    inverted: true
    mode:
      input: true
      pullup: true
  tolerance: 30%
  filter: 20us
  idle: 30ms
  buffer_size: 20kb

climate:
  - platform: aux_ir
    id: heat_pump
    name: "Heat Pump"
    transmitter_id: ir_tx
    receiver_id: ir_rx        # sync with the handheld remote
    use_fahrenheit: true      # exact 60–90 °F setpoints
    # sensor: room_temp       # optional current-temperature sensor

switch:
  - platform: aux_ir
    aux_ir_id: heat_pump
    turbo:
      name: "Turbo"
    eco:
      name: "Eco"
    sleep:
      name: "Sleep"
    health:
      name: "Health"
    fahrenheit:
      name: "°F Display"

button:
  - platform: aux_ir
    aux_ir_id: heat_pump
    display:
      name: "Display Toggle"
    iclean:
      name: "iClean"

number:
  - platform: aux_ir
    aux_ir_id: heat_pump
    timer:
      name: "Timer"
      mode: box
```

A complete configuration for the XIAO Smart IR Mate (TX GPIO3, RX GPIO4) is in [`examples/smart-ir-mate.yaml`](examples/smart-ir-mate.yaml).

### Climate options

| Option | Default | Description |
|---|---|---|
| `transmitter_id` | | The `remote_transmitter` to use |
| `receiver_id` | — | Optional `remote_receiver`; enables sync with the handheld remote |
| `use_fahrenheit` | `false` | Send and receive exact °F setpoints. The setpoint range becomes 60–90 °F. |
| `sensor` | — | Optional sensor shown as the current temperature |
| `supports_cool` / `supports_heat` | `true` | Hide modes your unit doesn't have |

All standard [climate](https://esphome.io/components/climate/) options are also supported.

### Optional entities

| Platform | Key | Behaviour |
|---|---|---|
| switch | `turbo` | Cool and Heat only. Forces High fan; turning it off restores the previous fan. |
| switch | `eco` | Cool only |
| switch | `sleep` | All modes except Fan |
| switch | `health` | Ionizer. Turns off whenever the unit powers off, as on the remote. |
| switch | `fahrenheit` | °C/°F display unit on the remote and indoor unit |
| button | `display` | Toggles the indoor unit's display. The display state isn't in the frame, so this is a button rather than a switch. |
| button | `iclean` | Self-clean. Only accepted while the unit is off. |
| number | `timer` | 0 cancels. 0.5–9.5 h in 0.5 h steps, then 10–24 h in whole hours. Setting it while the unit is on creates an off-timer; while off, an on-timer. |

If a request isn't allowed in the current mode (for example Turbo in Dry), nothing is sent, the switch flips back, and a warning is logged.

## How it behaves

- **Per-mode memory.** Switching modes from Home Assistant restores that mode's last setpoint and fan, including values set on the remote. Modes not used yet start with the remote's defaults: fan Auto, except Dry starts on Low and Fan mode on High.
- **While off,** nothing is sent except Power, °C/°F and iClean. Settings changed while off are applied at the next power on.
- **Aux heat.** In Heat mode the remote always sets the aux-heat bit, so the component does too. There's no control for it.
- **iFeel is not supported.** It's always sent off, and if someone turns it on with the remote, those bits are ignored.

## Logging

```yaml
logger:
  logs:
    aux_ir: DEBUG
```

Every frame is logged with its button code:

```
[D][aux_ir]: TX C3 87 E0 00 A0 00 20 00 00 20 00 06 70 (button 0x06)
[D][aux_ir]: RX C3 8F E0 00 A0 00 20 00 00 20 00 00 72 (button 0x00)
```

When reporting an issue, please include these lines.

## Protocol

The frame layout, button codes and quirks are documented in [`docs/protocol.md`](docs/protocol.md). Summary:

| Item | Value |
|---|---|
| Carrier | 38 kHz |
| Header | 9000 µs mark / 4500 µs space |
| Bit mark | 560 µs |
| 0 / 1 space | 580 µs / 1690 µs |
| Length | 13 bytes (104 bits), LSB first |
| Checksum | byte 12 = sum of bytes 0–11 |

## Testing

`tests/` contains two host-side test harnesses and the capture data they use.

**Protocol codec test.** Every captured frame must decode and re-encode byte for byte, and every single-bit corruption must be rejected.

**Component test.** This builds the real component against ESPHome on the host platform, then:
- replays a full capture session (real IR timings) into the receiver;
- for each remote button press, issues the equivalent Home Assistant command and checks the transmitted frame is identical to the remote's.

## Limitations

- **IR is one-way.** If the indoor unit misses a command, Home Assistant won't know.
- **Timer countdown.** The component counts the timer down itself and updates state when it expires, without transmitting anything.

## Contributing

Captures from other AUX models are the most useful contribution. Open an issue with:
- the indoor unit and remote model numbers;
- a few `RX` log lines;
- a note of which button you pressed for each.

## License

C++ code: GPLv3. Python code: MIT. This matches ESPHome's licensing.
