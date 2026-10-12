# Smart Pill Box - Build & Flashing Guide

This directory contains the firmware configured for the **minimal hardware setup**:
- **Microcontroller**: STM32F103C8T6 (Blue Pill)
- **Display**: 0.96" SSD1306 I2C OLED (128x64)
- **Inputs**: 3 Pushbuttons (Active LOW with internal pull-up resistors)
- **Disabled for now**: LEDs and Buzzer (reminders are handled visually on the OLED display)

---

## 1. Current Hardware Pinout & Wiring

| Peripheral | Blue Pill Pin | Peripheral Pin | Wiring Notes |
| :--- | :--- | :--- | :--- |
| **OLED VCC** | **3.3V** | VCC / VDD | Power (3.3V rail) |
| **OLED GND** | **GND** | GND | Common ground |
| **OLED SCL** | **PB6** | SCL | I2C1 Clock (Hardware I2C) |
| **OLED SDA** | **PB7** | SDA | I2C1 Data (Hardware I2C) |
| **Button 1** (Pill 1) | **PA0** | Pin 1 | Pin 2 to **GND** (Internal `INPUT_PULLUP`) |
| **Button 2** (Pill 2) | **PA1** | Pin 1 | Pin 2 to **GND** (Internal `INPUT_PULLUP`) |
| **Button 3** (Setup) | **PA2** | Pin 1 | Pin 2 to **GND** (Internal `INPUT_PULLUP`) |

> [!NOTE]
> All pushbuttons use internal pull-up resistors (`INPUT_PULLUP`). Connect one side of each switch to the designated STM32 pin and the other side directly to **GND**. When pressed, the pin reads `LOW`. No external resistors required for the buttons.

---

## 2. Flashing to STM32 Blue Pill

You can flash this firmware using either **PlatformIO** or **Arduino IDE**.

### Method A: PlatformIO (Recommended)
1. Open the project folder in VS Code / Antigravity:
   ```bash
   cd smart_pillbox
   ```
2. Connect your **ST-Link V2** programmer to the STM32 Blue Pill:
   - `SWDIO` -> `DIO` (Pin on the 4-pin header)
   - `SWCLK` -> `CLK` (Pin on the 4-pin header)
   - `GND`   -> `GND`
   - `3.3V`  -> `3.3V`
3. Build and upload:
   ```bash
   pio run -t upload
   ```
4. View serial output:
   ```bash
   pio device monitor
   ```

*(If using a USB-to-UART FTDI/CH340 module instead of ST-Link: Set jumper `BOOT0` to `1`, connect `PA9` to `RX` and `PA10` to `TX`, change `upload_protocol = serial` in [platformio.ini](file:///Users/harinarayanans/MC/smart_pillbox/platformio.ini), and reset the board before uploading).*

---

### Method B: Arduino IDE
1. Open the Arduino IDE.
2. Add STM32 Board support:
   - Go to **Preferences** -> **Additional Boards Manager URLs**:
     ```
     https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json
     ```
   - Go to **Tools** -> **Board** -> **Boards Manager**, search for `STM32` and install **STM32 MCU based boards** by STMicroelectronics.
3. Install required libraries from **Sketch** -> **Include Library** -> **Manage Libraries...**:
   - `Adafruit SSD1306`
   - `Adafruit GFX Library`
4. Configure board settings in the **Tools** menu:
   - **Board**: `Generic STM32F1 series`
   - **Board part number**: `BluePill F103C8`
   - **U(S)ART support**: `Enabled (generic 'Serial')`
   - **Upload method**: `STLink` (or `STM32CubeProgrammer (Serial)` if using UART)
5. Open [smart_pillbox.ino](file:///Users/harinarayanans/MC/smart_pillbox/smart_pillbox.ino) and click **Upload**.

---

## 3. How the Minimal System Works Right Now

1. **Simulated Clock**: Starts at `07:28:00`.
2. **First Alarm Test**: Default dose for Pill 1 is set to `07:30:00`. Two minutes after powering on, the reminder will automatically trigger.
3. **Visual OLED Reminder**:
   - The screen alternates with a flashing high-contrast banner: `*** MEDICINE! TAKE: PILL 1 ***`.
   - Press **Button 1** (`PA0`) to confirm taking Pill 1. The screen displays `DOSE TAKEN` and returns to idle.
   - If **Button 2** (`PA1`) is pressed accidentally, the screen displays a blinking inverted warning: `WRONG PILL! Press BUTTON 1`.
4. **Setup Menu**:
   - Press **Button 3** (`PA2`) in idle mode to enter the dose schedule editor.
   - **Button 1**: Change hour/minute values or toggle pills.
   - **Button 2**: Advance to next field or save changes.

---

## 4. How to Add the LEDs and Buzzer Later

When you get your physical LEDs and Buzzer, follow these two simple steps:

### Step 1: Wire the Components

| Component | STM32 Pin | Wiring Details |
| :--- | :--- | :--- |
| **Pill 1 LED** (Red) | **PB1** | Anode (+) -> 220Ω resistor -> **PB1**. Cathode (-) -> **GND** |
| **Pill 2 LED** (Green) | **PB10** | Anode (+) -> 220Ω resistor -> **PB10**. Cathode (-) -> **GND** |
| **Piezo Buzzer** | **PB0** | Positive pin (+) -> **PB0**. Negative pin (-) -> **GND** |

```
STM32 PB1  ----[ 220Ω Resistor ]---->| (Red LED Anode) ---- GND (Cathode)
STM32 PB10 ----[ 220Ω Resistor ]---->| (Green LED Anode) -- GND (Cathode)
STM32 PB0  -------------------------(+) [ Buzzer ] (-) ---- GND
```

### Step 2: Enable Feature Flags in Code

Open [src/main.cpp](file:///Users/harinarayanans/MC/smart_pillbox/src/main.cpp) (or [smart_pillbox.ino](file:///Users/harinarayanans/MC/smart_pillbox/smart_pillbox.ino)) and change lines 15–16:

```diff
-// Set to 1 when you connect the physical LED / Buzzer hardware!
-#define ENABLE_LEDS   0
-#define ENABLE_BUZZER 0
+// Enable physical hardware:
+#define ENABLE_LEDS   1
+#define ENABLE_BUZZER 1
```

Re-upload the code. That's all! The firmware will immediately:
- Initialize `PB1`, `PB10`, and `PB0` as outputs.
- Blink `LED_1` on Pill 1 reminders and `LED_2` on Pill 2 reminders.
- Sound a 1000 Hz tone on standard reminders, and switch to a fast 1500 Hz alarm pattern if the wrong pill button is pressed.

---

## 5. Directory Structure

```text
smart_pillbox/
├── platformio.ini       # PlatformIO build configuration
├── diagram.json         # Wokwi simulation diagram (OLED + Buttons + STM32)
├── libraries.txt        # Wokwi library dependencies
├── wokwi.toml           # Wokwi simulator entrypoint
├── INSTRUCTIONS.md      # This guide
├── smart_pillbox.ino    # Arduino IDE sketch file
└── src/
    └── main.cpp         # PlatformIO C++ source file
```
