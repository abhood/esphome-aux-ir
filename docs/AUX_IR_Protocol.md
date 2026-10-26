# AUX IR protocol: bit map v2

Sources:
- `aux_20261023_162035`: 569 frames. Covers temperature, °F, vanes, fan and mode.
- `aux_20261023_170442`: about 300 frames. Covers power, features, interactions, modes, timer and iFeel.

Every frame decoded to 104 bits with a valid checksum.

## Frame layout (13 bytes, LSB-first, 38 kHz, 9000/4500 header, 560 mark, 580/1690 space)

| Byte | Bits | Field | Values | Status |
|---|---|---|---|---|
| 0 | all | Header | `C3` | ✅ |
| 1 | 7–3 | Setpoint °C − 8 | 16–32 °C; `00000` in Fan/Auto | ✅ |
| 1 | 2–0 | Vertical vane | `111` fixed, `000` swing | ✅ |
| 2 | 7–5 | Horizontal vane | `111` (`E0`) fixed, `000` swing | ✅ |
| 3 | all | unused | `00` | ✅ |
| 4 | 7–5 | Fan | `A0` Auto, `60` Low, `40` Med, `20` High | ✅ |
| 4 | 4–0 | Timer hours | 0–24 | ✅ |
| 5 | 6 | Turbo | `40` (fan is also forced to High) | ✅ |
| 5 | 5–0 | Timer minutes | `00` or `1E` (30) | ✅ |
| 6 | 7–5 | Mode | `00` Auto, `20` Cool, `40` Dry, `80` Heat, `C0` Fan | ✅ |
| 6 | 4 | Health (ionizer), paired with byte 9 bit 1 | `10` | ✅ |
| 6 | 3 | iFeel | `08` | ✅ |
| 6 | 2 | Sleep | `04` | ✅ |
| 6 | 1 | °F display | `02` | ✅ |
| 7 | all | iFeel sensor temperature | `62`–`66` seen, scale unknown | ⚠️ |
| 8 | all | iFeel sensor °F (iFeel + °F only) | `48` = 72 °F | ⚠️ one sample |
| 9 | 7 | On-timer active (unit off) | `80` | ✅ |
| 9 | 6 | Off-timer active (unit on) | `40` | ✅ |
| 9 | 5 | Power | `20` on | ✅ |
| 9 | 4 | Auxiliary (electric) heat | `10`, on by default in Heat; this unit has no aux heater | ✅ |
| 9 | 3 | Eco | `08` | ✅ |
| 9 | 2 | iClean self-clean (button pressed while off) | `04` | ✅ |
| 9 | 1 | Health (ionizer), paired with byte 6 bit 4 | `02` | ✅ |
| 10 | all | °F setpoint | `0x80 \| (°F − 56) << 1` in °F, `00` in °C | ✅ |
| 11 | all | Button code | table below | ✅ |
| 12 | all | Checksum | sum of bytes 0–11 | ✅ |

```
°C    = (b1 >> 3) + 8
°F    = ((b10 & 0x7F) >> 1) + 56          when b10 & 0x80
timer = (b4 & 0x1F) + (b5 & 0x3F) / 60    hours
```

## Button codes (byte 11)

| Code | Button | Code | Button |
|---|---|---|---|
| `00` | Temp ▲ | `0B` | Sleep |
| `01` | Temp ▼ | `0D` | Timer (set and cancel) |
| `02` | Vertical swing | `13` | Eco (Cool) |
| `03` | Horizontal swing | `15` | Display (state not stored in the frame) |
| `04` | Fan | `16` | Eco button in Heat → Aux heat toggle |
| `05` | Power | `17` | °C/°F |
| `06` | Mode | `19` | iClean/Health pressed while off → iClean |
| `07` | iClean/Health pressed while on → Health toggle | `1E` | iFeel |
| `08` | Turbo | | |

## Behaviour

**Aux heat.** The remote sets it whenever Heat is selected, and the Eco button toggles it there. The indoor unit has no auxiliary heater, so the bit has no effect. To match the remote, send it set in Heat and don't expose a control for it.


**Per-mode memory.** Each mode keeps its own setpoint and fan.

| Mode | Setpoint | Fan options | Turbo | Eco | Sleep |
|---|---|---|---|---|---|
| Cool | own | Auto/Low/Med/High | ✅ | ✅ (`13`) | ✅ |
| Heat | own | Auto/Low/Med/High | ✅ | button toggles Aux heat (`16`) | ✅ |
| Dry | own (24 °C default) | Auto/Low/Med/High, default Low | ✗ | ✗ | ✅ |
| Fan | none (`00000`) | Low/Med/High only, default High | ✗ | ✗ | ✗ |
| Auto | none (`00000`) | Auto/Low/Med/High | ✗ | ✗ | ✅ |

"✗" means the remote sends nothing. Vanes, iFeel, Display, Health and Timer work in every mode.

**Turbo.** Turbo forces High. Turning it off restores the previous fan. Pressing Fan while Turbo is on cancels Turbo and steps to the next fan speed after the saved one.

**Independence.** Sleep, Eco, Turbo, Health, both vanes and °F all held together in one frame without interfering.

**Power.**
- Power off clears byte 9 bits 5 and 4 and Health. Everything else is kept.
- Power on restores the Aux heat bit.
- While off, only iClean/Health (starts iClean, switches the frame to Fan/High) and °C/°F send a frame.

**Timer.**
- Off-timer: byte 9 `40` plus hours and minutes.
- On-timer (unit off): byte 9 `80`.
- The timer stays in every frame until it's cancelled. Cancelling sends button `0D` with hours/minutes zeroed.

**iFeel.** The sensor bytes are sent with every frame while iFeel is on. There were no periodic frames in 2 minutes.

## Still unresolved

| Item | What to capture |
|---|---|
| **iFeel scale** | Put a thermometer next to the remote at 2–3 known temperatures, press Temp ▲▼ at each, in °C and in °F |
| On-timer + off-timer together | Set both if the remote allows it |
| Vane fixed positions | Whether any other byte 1 / byte 2 values exist (only fixed and swing seen) |

## Model errors in session 2 (log state, not frames)
- The plan's `clean` state was really Health, and is inverted from seq 39 onward.
- In Heat, `eco` actually toggled Aux heat (byte 9 bit 4).
- Dry, Heat and Fan modes keep their own fan settings, so some logged fan values are wrong.
