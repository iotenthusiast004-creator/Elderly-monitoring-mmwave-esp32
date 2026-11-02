# Elderly Patient / Individual Monitoring System

Privacy-friendly monitoring prototype that combines two Ebyte 24 GHz mmWave radar modules on a single ESP32:

- **E54-24LD12B (E54 B)**: presence sensing, including a person lying completely still
- **E54-24LD12D (E54 D)**: multi-target tracking (position, speed, up to 3 people)

No camera and nothing to wear. The two modules cover each other's weaknesses, which is the point of the demo.

> **Disclaimer:** This is a hobby / educational prototype. It is **not** a certified medical device, it does **not** output breathing or heart-rate values, and it must not be relied on for patient safety.

📺 **Demo video:** [YOUR YOUTUBE LINK]

![System overview](docs/overview.png) <!-- add a photo or the block diagram -->

---

## Contents

1. [What each module can and cannot do](#what-each-module-can-and-cannot-do)
2. [Hardware](#hardware)
3. [Wiring](#wiring)
4. [Software setup](#software-setup)
5. [Build it step by step](#build-it-step-by-step)
6. [Using the sketches](#using-the-sketches)
7. [Using Ebyte's PC tool through the ESP32](#using-ebytes-pc-tool-through-the-esp32)
8. [Mounting tips](#mounting-tips)
9. [Demo scenarios](#demo-scenarios)
10. [Troubleshooting](#troubleshooting)
11. [Repository layout](#repository-layout)
12. [References](#references)
13. [License and contact](#license-and-contact)

---

## What each module can and cannot do

| | E54 B (E54-24LD12B) | E54 D (E54-24LD12D) |
|---|---|---|
| Output | Presence state, moving / stationary distance and energy, presence GPIO | X/Y position, speed, up to 3 targets |
| Range / angle | 4.5 m, ±45° | 8 m, ±60° azimuth |
| Accuracy | 0.75 m range gates (readings under 1.5 m show as 1.5 m) | 0.15 m ranging, 2° angle |
| Motionless person | Yes (micro-motion) | Track held about 37 s by default, then lost |
| People count | No | Up to 3 |
| Direction / speed | No | Yes |
| Supply | 3.0–3.6 V, about 50 mA | 3.6–5.5 V, about 66 mA at 5 V |
| Mounting | Wall 1.5–2 m or ceiling 2.6–3 m | Wall 1.4–1.7 m |

Neither module reports body height or vital-sign values. A fall can only be *inferred* from a speed spike followed by a person who stays still outside the bed zone.

---

## Hardware

| Qty | Part |
|---|---|
| 1 | ESP32 DevKit (30/38-pin, e.g. DOIT ESP32 DevKit V1) |
| 1 | Ebyte E54-24LD12B 24 GHz presence module |
| 1 | Ebyte E54-24LD12D 24 GHz multi-target tracking module |
| 1 | USB cable for the ESP32 |
| – | Jumper wires; header pins to solder onto the E54 B (its holes come unpopulated) |
| 2 | 100 µF + 0.1 µF capacitors per module (supply decoupling) |
| 2 | Optional: 1 kΩ + 2 kΩ resistors for a voltage divider on the E54 D TX line (see below) |

---

## Wiring

Both radars talk to the ESP32 over UART at **256000 baud, 8N1**. TX and RX are crossed.

| ESP32 | E54 B (J1) | E54 D |
|---|---|---|
| 3V3 | pin 5 `3V3` | not used |
| VIN / 5V | not used | `5V` |
| GND | pin 4 `GND` | `GND` |
| GPIO25 (RX1) | pin 3 `O_T1` (module TX) | |
| GPIO26 (TX1) | pin 2 `RX` | |
| GPIO27 | pin 1 `O_T2` (HIGH = occupied) | |
| GPIO16 (RX2) | | `TX` |
| GPIO17 (TX2) | | `RX` |

```
          +--------------------+
 E54 B ---| GPIO25/26 (UART1)  |
 (3.3 V)  | GPIO27 presence    |--- USB ---> PC
          |       ESP32        |
 E54 D ---| GPIO16/17 (UART2)  |
 (5 V)    +--------------------+
```

Notes:

- **E54 D connector:** J1 on the module is a small connector. If you don't have the mating plug, solder to the J2 pads (5V, GND, TXD, RXD).
- **E54 D logic level:** the manual does not state it. Measure the module's TX pin with a multimeter. If it reads about 5 V, put a 1 kΩ / 2 kΩ divider between the E54 D TX and GPIO16.
- **Power:** the E54 D must be powered from 5 V (VIN while on USB), never from 3V3. Keep the supply clean; add 100 µF + 0.1 µF near each module.

![Wiring diagram](docs/wiring.png) <!-- add your block diagram -->

---

## Software setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Add ESP32 board support (Boards Manager → "esp32" by Espressif).
3. Select your board (e.g. *DOIT ESP32 DEVKIT V1*) and the correct COM port.
4. No extra libraries are required.

---

## Build it step by step

1. **Wire** both modules as in the table above.
2. **Check the E54 D TX voltage** before connecting it to GPIO16 (see notes).
3. **Flash `E54B_presence`** and open the Serial Monitor at **115200** baud. You should see CSV lines and presence changes.
4. **Flash `E54D_tracking`** and open the Serial Monitor at 115200 baud. Walk in front of the radar and watch X/Y and speed.
5. **Mount** both radars on the same wall, parallel and facing the room, at least about 1 m apart (see [Mounting tips](#mounting-tips)).
6. (Optional) **Flash `E54D_usb_bridge`** to use Ebyte's PC visualisation tool with the E54 D through the ESP32.

Each sketch uses its own UART, so the wiring stays the same when you switch between sketches. Only one sketch runs on the ESP32 at a time.

---

## Using the sketches

### `E54B_presence`

Output is CSV, one line per radar frame:

```
ms,state,moving_cm,moving_energy,static_cm,static_energy,gpio
```

Lines starting with `#` are events (for example, the GPIO changing between OCCUPIED and EMPTY).

Keys typed in the Serial Monitor:

| Key | Action |
|---|---|
| `e` / `n` | Engineering mode on / off (adds per-gate energy values) |
| `3` / `6` | Limit the maximum range gate to 3 (about 2.25 m) or 6 (about 4.5 m, default) |
| `h` / `d` | High (50/50) or default (20/25) sensitivity |
| `u` / `t` | Unoccupied delay 1 s or 5 s (default) |
| `v` / `p` | Read firmware version / parameters (raw hex reply) |

### `E54D_tracking`

Output is CSV, one line per active target, about 10 Hz:

```
ms,id,x_mm,y_mm,speed_cm_s,dist_m,angle_deg,in_bed_zone
```

Events (`# T1 APPEARED`, `# T1 FAST MOVE`, `# T1 LOST after N s without motion`) show the tracking behaviour.

| Key | Action |
|---|---|
| `1` | Single-target mode |
| `3` | Three-target mode |

Edit these constants at the top of the sketch for your room:

- `BED_X_MIN`, `BED_X_MAX`, `BED_Y_MIN`, `BED_Y_MAX`: the bed-zone rectangle in millimetres (radar at the origin, Y straight ahead)
- `MOVE_CMS`, `FAST_CMS`: speed thresholds for "moving" and "fast move"
- `D_WRAP_CONFIG`: wraps mode commands in enable/end-config frames. Set it to `0` if the ACKs look odd.

The angle sign depends on the mounting orientation. Flip it if your view is mirrored.

---

## Using Ebyte's PC tool through the ESP32

The E54 D is wired to the ESP32, not the PC, so Ebyte's `RF_Setting_E54_D` tool cannot see it directly. Flash `E54D_usb_bridge` and the ESP32 becomes a transparent USB-to-serial bridge:

1. Keep the wiring unchanged and flash `E54D_usb_bridge`.
2. Close the Arduino Serial Monitor.
3. Run `RF_Setting_E54_D_V1.0.exe` (download it from the [Ebyte website](https://www.cdebyte.com)), select the ESP32's COM port at **256000** baud, and click **Detect Device**.
4. Click **Start** to see the targets. Use *Region Monitoring* to draw zones and blind areas.

Many ESP32 boards reset when a program opens the COM port. If detection fails the first time, wait a moment and click **Detect Device** again, or add a 10 µF capacitor between EN and GND.

---

## Mounting tips

- Mount both modules on the same wall, parallel, facing the same direction, and not facing each other. Keep them at least about 1 m apart. If you see ghost targets, test with one module powered off.
- E54 B: wall at 1.5–2 m, or ceiling at 2.6–3 m (keep ceiling-mounted radars at least 3 m apart).
- E54 D: wall at 1.4–1.7 m, ±60° around the normal direction.
- Avoid moving curtains, fans, plants and large reflective surfaces in front of the radars, and keep the mounting firm. Movement behind a radar can show up in its back lobe. A metal back plate can reduce this.
- Any enclosure must be transparent at 24 GHz (no metal).

---

## Demo scenarios

1. **Lie still in bed:** the E54 B stays `STATIONARY` and the presence pin stays high. The E54 D eventually logs `LOST after N s without motion`.
2. **Limit the range:** press `3` on the E54 B, someone beyond about 2.25 m disappears; press `6` to restore.
3. **Walk across the room:** the E54 D gives position, angle and speed; the E54 B only gives "moving" and a distance.
4. **Several people:** the E54 D tracks up to three; the E54 B only says "occupied".
5. **Bed exit and gait speed:** use the bed-zone flag and the speed column of the E54 D.
6. **Possible-fall pattern (on a crash mat, never a real fall):** a fast move followed by a person who stays still outside the bed zone, while the E54 B still reports stationary presence. This is a heuristic, not a certified detector.

---

## Troubleshooting

| Problem | Things to check |
|---|---|
| No data from a module | TX/RX crossed? Baud 256000? Correct supply voltage (3.3 V for E54 B, 5 V for E54 D)? |
| Garbage characters | Wrong baud rate, or something else printing on the radar UART |
| E54 B reports 1.5 m when very close | Expected: the manual says readings under 1.5 m are not accurate |
| E54 D target disappears when you stop moving | Expected: the track is held for about 37 s, then dropped |
| Ghost targets | Radars facing each other or too close; reflective surfaces; fans or curtains |
| Commands get no ACK | Send enable-config first, finish within the 3 s configuration window; try `D_WRAP_CONFIG` |
| PC tool can't find the device | Close the Serial Monitor, wait for the ESP32 to restart, click Detect Device again |

---

## Repository layout

```
.
├── E54B_presence/
│   └── E54B_presence.ino      # E54-24LD12B presence demo
├── E54D_tracking/
│   └── E54D_tracking.ino      # E54-24LD12D tracking demo
├── E54D_usb_bridge/
│   └── E54D_usb_bridge.ino    # USB <-> UART bridge for Ebyte's PC tool
├── docs/                      # wiring diagram, limitation chart, photos
└── README.md
```

---

## References

- Ebyte E54-24LD12B user manual (V1.2)
- Ebyte E54-24LD12D product specifications (v1.0)
- Ebyte website: <https://www.cdebyte.com>

Code was written from these manuals; check parameters against your own units.

---

## License and contact

Released under the MIT License (add a `LICENSE` file).

- GitHub: [YOUR GITHUB PROFILE]
- LinkedIn: [YOUR LINKEDIN LINK]
- Reddit: [YOUR REDDIT LINK]
