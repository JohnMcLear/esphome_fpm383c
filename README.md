# esphome_fpm383c

ESPHome external_component for the **HLK-FPM383C** capacitive fingerprint
module. Exposes the sensor's full operational surface to Home Assistant:
finger detection, identification, enrollment, deletion, and per-operation
status — all driven by a single hub class over UART.

> **Status:** Runs on a real FPM383C (ESP32-S3-DevKitC-1, and ESP32-P4 through a WK2132 I2C-UART bridge).
> Not yet production-hardened. See [Security](#security) before deploying.
>
> **Security advisory, 14 Oct 2026: versions before this fix reported a match for ANY finger.** The `MATCH_SYNC`
> reply was decoded wrongly: an error code of 0 was taken as "matched", but 0 only means the command ran. A
> non-enrolled finger returned error 0 with result 0 and a stale template id, which the component published as
> `match: slot N`, fired the green LED and set `Last Match`. The decision now requires the result word
> (bytes 21..22) to equal 1, fails closed on anything else, and is covered by host unit tests built from real frames
> (`tests/`). If you drove anything from a match with an earlier version, update.

## What you get

Per FPM383C reader instance, the following Home Assistant entities are
available:

| Entity | Type | Purpose |
| --- | --- | --- |
| `<name> Finger` | `binary_sensor` | Polled at 2 Hz; reflects finger presence on the glass. `device_class: occupancy`. |
| `<name> Status` | `text_sensor` | Free-text state: `idle`, `matching`, `match: slot N`, `no match`, `enrolling N/100`, `enroll OK: slot N`, `enroll fail: 0xXXXXXXXX`, `enroll timeout`, `cancelling`, `cancelled`, `deleting slot N`, `deleting all`, `delete OK`, `delete fail: 0xXXXXXXXX`, `match timeout`, `delete timeout`, `busy`. |
| `<name> Last Match` | `text_sensor` | Slot id of the most recent successful match (`""` if never). |
| `<name> Slot` | `number` | Selects target slot for enroll/delete. Range 1–64; default 1. (Persistence across reboots is on the roadmap; the value resets to its minimum on each boot today.) |
| `<name> Enroll` | `button` | Starts a 6-capture enrollment (`AUTO_ENROLL`, cmd `01 18`) into the current `Slot`. The sensor drives the multi-step finger-place / lift / re-place flow internally; the component surfaces progress per frame. |
| `<name> Cancel` | `button` | Aborts an in-flight enrollment (`CANCEL`, cmd `01 15`). |
| `<name> Delete` | `button` | Deletes the fingerprint at the current `Slot` (`DELETE`, cmd `01 31`). |
| `<name> Delete All` | `button` | Wipes every enrolled fingerprint (`DELETE` mode 0x01). |

### Behaviour

- **Polling.** A 500 ms `CHECK_FINGER_STATUS` (cmd `01 35`) poll keeps
  `<name> Finger` accurate within ~500 ms.
- **Edge-triggered match.** The instant `<name> Finger` rises, the hub
  fires a `MATCH_SYNC` (cmd `01 23`). On match, the slot id is published
  to `<name> Last Match` and `<name> Status` shows `match: slot N`.
  LED solid green for 1 s. On no-match, `<name> Status` reads `no match`
  and the LED flashes red.
- **Hub-owned LED.** All LED feedback (cmd `02 0F`) is driven by the
  hub. Green for match/enroll-success/delete-success. Red for failures
  and timeouts. Blue blink during enrollment between captures.
- **Op-state machine.** A single state variable
  (`IDLE / MATCHING / ENROLLING / DELETING`) gates concurrent operations.
  Pressing Enroll while another op is in flight returns `busy` and
  ignores the press.
- **Watchdogs.** 2 s for match and delete; 60 s overall + 20 s per
  enrollment progress-advance. The 20 s watchdog tracks *progress
  changes*, not frame arrivals — the sensor sometimes emits many
  "stuck at N" frames during user pauses.
- **Cancel-on-boot.** The FPM383C's enrollment / match state survives
  ESP reboots. `setup()` sends `CANCEL` before the heartbeat so any
  in-flight operation aborts cleanly before polling resumes.
- **Multi-instance.** Declared `MULTI_CONF = True`. You can wire two
  readers to two UARTs and run two independent hub instances on a
  single ESP32.

## Hardware

The HLK-FPM383C ships as a 6-pin module (1.0 mm pitch). On the
ESP32-S3-DevKitC-1:

| FPM383C pin | Signal     | ESP32-S3 GPIO |
| ----------- | ---------- | ------------- |
| 1           | V_TOUCH    | 3V3           |
| 3           | VCC        | 3V3           |
| 4           | TX         | a free UART RX (e.g. GPIO18) |
| 5           | RX         | a free UART TX (e.g. GPIO17) |
| 6           | GND        | GND           |

Pin 2 (`TOUCHOUT`) is intentionally unused — finger detection runs over
UART, not the GPIO interrupt path. Both pin 1 (V_TOUCH) and pin 3 (VCC)
must be wired to 3.3 V.

UART defaults: **57600 baud, 8N1**. Avoid strapping pins (GPIO0/45/46),
the USB-JTAG pair (GPIO19/20), and flash/PSRAM pins.

## Quick start

In your ESPHome YAML:

```yaml
external_components:
  - source: github://JohnMcLear/esphome_fpm383c
    components: [fpm383c]

uart:
  id: fpm_uart_a
  tx_pin: GPIO17
  rx_pin: GPIO18
  baud_rate: 57600

fpm383c:
  - id: fpm_a
    uart_id: fpm_uart_a
    poll_interval: 500ms

binary_sensor:
  - platform: fpm383c
    fpm383c_id: fpm_a
    type: finger
    name: "FPM383C A Finger"

text_sensor:
  - platform: fpm383c
    fpm383c_id: fpm_a
    type: status
    name: "FPM383C A Status"
  - platform: fpm383c
    fpm383c_id: fpm_a
    type: last_match
    name: "FPM383C A Last Match"

number:
  - platform: fpm383c
    fpm383c_id: fpm_a
    type: slot
    name: "FPM383C A Slot"

button:
  - platform: fpm383c
    fpm383c_id: fpm_a
    type: enroll
    name: "FPM383C A Enroll"
  - platform: fpm383c
    fpm383c_id: fpm_a
    type: cancel
    name: "FPM383C A Cancel"
  - platform: fpm383c
    fpm383c_id: fpm_a
    type: delete
    name: "FPM383C A Delete"
  - platform: fpm383c
    fpm383c_id: fpm_a
    type: delete_all
    name: "FPM383C A Delete All"
```

A complete runnable example lives at
[`examples/single-reader.yaml`](examples/single-reader.yaml).

## Enrolling a finger

1. In Home Assistant, set `<name> Slot` to the slot number you want to
   use (1–64).
2. Press `<name> Enroll`.
3. Watch the `<name> Status` text sensor — it will tick from
   `enrolling slot N` through `enrolling 16/100`, `33/100`, etc., until
   `enroll OK: slot N`. You'll need to follow the sensor's lead, lifting
   and re-placing the same finger as it asks (the LED blinks blue
   between captures; total flow takes ~15–25 seconds).
4. If you walk away or stop placing the finger, the 60 s overall
   watchdog (or the 20 s per-stage watchdog) will fire, send `CANCEL`,
   and reset state to idle. Just press Enroll again.

## Identifying a finger

It's automatic. As soon as a finger touches the glass, the hub sees the
rising edge from its 2 Hz poll and fires `MATCH_SYNC`. Within ~150 ms
you see one of:

- `<name> Status` = `match: slot 3`, `<name> Last Match` = `3`, green LED.
- `<name> Status` = `no match`, red LED.

## Hub configuration options

```yaml
fpm383c:
  - id: <required>          # canonical id for binding child platforms
    uart_id: <required>     # the UART bus to drive
    poll_interval: 500ms    # finger-presence poll cadence (default 500ms)
```

There are no other tunables today; watchdogs, LED durations, and the
6-capture enroll count are baked in. If you need to change them, fork
and tune `fpm383c.cpp`.

## Protocol notes (for the curious)

The protocol is documented by the
[Mark7888/hlk-fpm383c Arduino driver](https://github.com/Mark7888/hlk-fpm383c).
A few empirical deviations on the firmware revision tested:

- **`MATCH_SYNC` "matched" byte is unreliable.** The byte at frame
  offset 21 (Mark7888's `data[0]` after the error code) stays `0x00`
  even on a successful match. The component uses the **error code at
  offsets 17–20** (`0` = success) as the reliable indicator instead.
  The slot id at offsets 25–26 BE matches Mark7888 and is correct.
- **`AUTO_ENROLL` intermediate frames may carry non-zero error codes.**
  Observed values `0x11` and `0x12` mid-flow despite the enrollment
  ultimately succeeding — these are status codes (retry / place again),
  not failures. The hub ignores non-zero error codes in non-terminal
  frames; only `count == 0xFF && progress == 100` indicates success.
- **`enter_sleep` (cmd `02 0C`) silences the sensor.** Once in sleep
  mode, the sensor ignores `CHECK_FINGER_STATUS` until a finger touch
  wakes it. The component does **not** send sleep commands; if you add
  them, polling will stop working.

## Security

> **TL;DR — for hobby and HA-exposure use, the component is fine. For
> anything load-bearing (door strikes, safes, payments), the FPM383C
> protocol does not give you the primitives to defend against a sensor
> swap. Plan accordingly.**

This component is appropriate for HA exposure: surfacing a fingerprint
match into Home Assistant for automations the user controls. It is
**not** a security boundary.

### Threat model

The FPM383C is a UART-only bus device with no cryptographic identity.
There is no challenge-response, no signed messages, and no per-device
key. This means:

- **Sensor swap.** An attacker with physical access to the wiring can
  unplug the legitimate sensor and substitute a different FPM383C
  module that has the attacker's fingerprint enrolled in a "trusted"
  slot. The ESP32 has no way to tell. Match results from the new
  sensor will publish identically to the old one.
- **UART tap.** With the case open, an attacker can sniff the UART line
  and capture the 4-byte frame password (default `00 00 00 00`)
  exchanged with the sensor. They can replay or mimic frames.
- **Slot identity is just an index.** `<name> Last Match: 3` does not
  mean "user X" — it means "slot 3 in whatever module is currently
  plugged in." HA-side automations should treat the slot number as
  *advisory*, not as proof of identity for sensitive actions.

### Mitigations available in the protocol

The component does **not** implement these today. If you need them,
contributions welcome:

1. **Set a non-default frame password.** The `setPassword` command
   (cmd `02 01`) lets you replace the default `00 00 00 00` with a
   random 4-byte value stored in `secrets.yaml` and provisioned at
   commission time. A swapped sensor with its default password would
   then reject every command. **Caveats:** only 32 bits, brute-forceable
   in software; sniffable on a tapped UART line; defeated by
   factory-resetting the swapped module.
2. **Pin the module id.** `getModuleId` (cmd `03 01`) returns a
   per-device identifier. Read it once during commissioning, persist
   it to ESP NVS, and refuse to start polling on boots where the read
   value differs. Whether this is forgeable depends on whether the
   FPM383C's module id is hardware-fused or writable — needs probing
   per-firmware-revision.

### Mitigations outside the protocol

- **Tamper-evident enclosure.** A case-intrusion switch on a GPIO line,
  or a known-resistance loop in the wiring harness that breaks when a
  wire is unclipped. Physical layer beats protocol layer for "did
  someone touch the assembly" questions.
- **Defense in depth in HA.** Couple match events with a second factor
  (presence sensor, time-of-day window, an "armed" state that requires
  another control surface to disengage) before triggering anything
  load-bearing.
- **Don't physically expose the sensor to attackers.** Trivial but
  often forgotten. A fingerprint reader behind a locked door can't be
  swapped by someone outside that door.

If your deployment requires defending against a determined attacker
with hands on the hardware, the FPM383C is the wrong sensor. Look at
modules with secure-element-backed mutual authentication.

## Credits

Protocol details, frame layouts, and command codes were derived from
[Mark7888/hlk-fpm383c](https://github.com/Mark7888/hlk-fpm383c) (an
Arduino driver for the same module family). This component is an
ESPHome native rewrite, not a wrapper around that library, but the
protocol homework is theirs.

The HLK-FPM383F user manual (a sibling part with the same protocol
family) is the canonical vendor reference for command numbering and
LED modes.

## License

Apache License 2.0 — see [LICENSE](LICENSE).
