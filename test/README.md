# OLED-Only Diagnostic Test Firmware

Target: **STM32F103C8T6 (Blue Pill)**  
Display: **SSD1306 128x64 Monochrome OLED (I2C)**  
Location: `test/main_oled_test.c`

---

## 1. Overview
This test is **strictly dedicated to the SSD1306 OLED display**.
- **Zero dependencies**: No RTC (DS1307), No SD card, No pushbuttons, No buzzer.
- **Grain-Free Initialization**: Clears the GDDRAM before sending `0xAF` (Display ON) to prevent static snow/grain flash on power up.
- **Auto-Detection**: Probes both standard address `0x3C` (`0x78`) and alternate address `0x3D` (`0x7A`).
- **Hardware Heartbeat**: The Blue Pill's onboard LED on **PC13** toggles on every frame so you know the MCU is active.

---

## 2. Hardware Wiring

| OLED Pin | STM32F103 Pin | Note |
|---|---|---|
| **GND** | **GND** | Ground |
| **VCC** | **3.3V** | Power (use 3.3V, not 5V) |
| **SCL** | **PB6** | I2C1 Clock (Hardware I2C) |
| **SDA** | **PB7** | I2C1 Data (Hardware I2C) |

*Optional:*
- **PC13**: Onboard LED (Heartbeat toggle on each frame; blinks fast if OLED is not detected).

---

## 3. How to Run

### Method 1: Direct in STM32CubeIDE (Recommended)
1. Open [`Core/Src/main.c`](../Core/Src/main.c).
2. Replace its content with [`test/main_oled_test.c`](./main_oled_test.c) (or backup your current `main.c`).
3. Click **Project -> Build Project** (`Ctrl + B`).
4. Click **Run** or **Debug** to flash the target via ST-Link.

---

## 4. Visual Test Sequence (Infinite Loop)

1. **Test 1 — Device Info & Splash**:
   - Pings OLED I2C address (`0x3C` / `0x3D`).
   - Displays detected address, resolution (`128x64`), and bus interface.
2. **Test 2 — Border Alignment & Crosshairs**:
   - Draws a 1-pixel rectangle at `(0, 0, 127, 63)` to verify every edge pixel is visible.
   - Draws diagonal lines and center targeting circles to check horizontal/vertical geometry.
3. **Test 3 — Pixel Check & Checkerboard**:
   - Full screen solid white (`0xFF`) -> solid black (`0x00`) -> 8x8 checkerboard.
   - Inspect for dead pixels or stuck lines.
4. **Test 4 — Typography & Character Sizes**:
   - Standard 5x7 ASCII font, numbers, symbols, size 2X text, and inverted highlighted text banner.
5. **Test 5 — Real-time Animation & FPS Progress Bar**:
   - Bouncing ball with live physics box.
   - Smooth progress bar (0% to 100%) running over 6 seconds to verify stable I2C data streaming without timeouts.

---

## 5. Troubleshooting Checklist

- **Screen is completely blank / LED PC13 blinks rapidly**:
  - The MCU cannot find the display on the I2C bus.
  - Verify that SCL goes to **PB6** and SDA goes to **PB7**.
  - Ensure VCC is receiving a steady 3.3V.
- **Screen shows partial or distorted lines**:
  - Ensure standard 4.7kΩ pull-up resistors are present on SDA/SCL (most breakout modules already include these onboard).

