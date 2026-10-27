# Wiring – ESP32 ↔ KORTLINJE

This is the reference wiring for FLYGLINJE. A step-by-step photo guide (German) lives at [bastelbyra.de/umbau](https://bastelbyra.de/umbau/).

The original controller (U3) stays on the board but is disconnected from everything the ESP32 takes over.

> ⚠️ **Before every power-on:** measure each wire against P2 `VDD` and P2 `GND` – no beep allowed. Check the removed-resistor pads against each other – no bridges.

## 1. Display (required)

The clock uses three VK1640 display drivers (U1, U2, U4). Remove the series resistor in each CLK/DATA line and solder a wire to the pad on the **driver-chip side**.

| Signal | Remove | Note | ESP32 |
|---|---|---|---|
| U1 CLK | R1 | pad towards VK1640 pin 8 | GPIO18 |
| U1 DATA | R37 | VK1640 pin 7 | GPIO19 |
| U2 CLK | R4 | right one in the double footprint | GPIO21 |
| U2 DATA | R12 | left one in the double footprint (R13/R23 stay) | GPIO22 |
| U4 CLK | R77 | sits next to U3, not next to U4 | GPIO23 |
| U4 DATA | R78 | next to U3 | GPIO25 |

## 2. Power

| Clock | ESP32 | Note |
|---|---|---|
| P2 `VDD` (5 V from the power cable) | `VIN` | through a **1N5817**, ring towards the ESP |
| P2 `GND` | `GND` | |

The P1 header (`G·DA·CK·V`) consists of flat test pads and is hard to solder – use P2.

D44 stays original. Only if the display flickers or the voltage sags, replace it with a 1N5408 (stripe on the same side).

## 3. Buttons and buzzer (cut-and-desolder method)

On each connector, desolder two wires from the board. The **free end** (button/buzzer side) goes to the ESP; the board end stays unconnected.

| Connector | Wire | Function | Target |
|---|---|---|---|
| TOP-KEY (3-pin) | 1 white | 0 V | stays |
| | 2 red | button signal | desolder → **GPIO34** |
| | 3 black | 3.3 V | desolder → ESP `3V3` |
| POWER / P1 (4-pin) | 1 blue | 3.3 V | desolder → ESP `3V3` |
| | 2 black | button signal | desolder → **GPIO35** |
| | 3 red | 0 V | stays |
| | 4 white | BAT+ | stays – **never to the ESP** |
| BUZ (5-pin, B1–B5, next to Q1/R30/R31) | B1 white | buzzer − | desolder → ESP `GND` |
| | B2 red | buzzer + | desolder → **GPIO26** |
| | B3–B5 | – | stay |

**Cross-check before connecting:** clock on its power cable, ESP disconnected → the free wire end must read 0 V. If it reads 3.3 V, you are holding the board end – never connect that to the ESP.

**Measure the buzzer:** resistance between the free ends. Open / kΩ = piezo → connect directly. Below 100 Ω → put a 100 Ω resistor in the red wire.

Original button functions: TOP-KEY (top) = snooze, brightness, mode, alarm/tone. POWER (back) = alarm, set, plus, minus. The buttons are a resistor ladder read via ADC1 (ADC2 can't be used while Wi-Fi is on).

## 4. Optional sensors (I²C)

| Module | Pins |
|---|---|
| AHT20 / SHT30, DS3231 | SDA GPIO32, SCL GPIO33, `3V3`, `GND` |

## Pin summary

| GPIO | Use |
|---|---|
| 18 / 19 | U1 CLK / DATA |
| 21 / 22 | U2 CLK / DATA |
| 23 / 25 | U4 CLK / DATA |
| 26 | buzzer |
| 32 / 33 | I²C SDA / SCL (optional) |
| 34 / 35 | TOP-KEY / POWER buttons (ADC1) |

Avoided on purpose: GPIO 0, 2, 5, 12, 15 (strapping pins) and 6–11 (flash).

## Lessons learned

- Put pin headers on the ESP and use Dupont cables – the ESP stays removable for flashing.
- Never plug USB into the ESP while the clock is powered without the diode in `VIN`.
- Build step by step and test after each display driver.
- If the supply voltage collapses once the ESP is connected, check the R1 replacement wire first – it is easy to hit the wrong U1 pin.

## Sources

- Connector pinout and further board signals: [VIPnytt/Kortlinje wiki](https://github.com/VIPnytt/Kortlinje) (MIT)
- Display pads and the cut-and-desolder method were first documented in [kobebeefg/ikea-clock-lab](https://github.com/kobebeefg/ikea-clock-lab) – used as a reference only, no material copied.
