# Agent Development & Modification Log

This file tracks all modifications, driver implementations, bug fixes, and architectural decisions made by the AI coding assistant across project sessions.

---

## Session Date: 2026-10-31

### 1. Codebase Audit & Initial State Analysis
* **Initial State**:
  * Project configured for STM32F103C8T6 with SSD1306 pure-C driver, push buttons, LEDs, buzzer.
  * Simulated clock in `main.c` ticking from `07:29:50` using `HAL_GetTick()`.
  * Setup menu supported editing Pill 1 and Pill 2 times only.

---

### 2. Implementation of DS1307 Real-Time Clock Driver
* **Files Created**:
  * [`Core/Inc/ds1307.h`](file:///Users/harinarayanans/Documents/Oled_display/Core/Inc/ds1307.h): Register map, `RTC_DateTime_t` definition, BCD/DEC conversion macros (`BCD_TO_DEC`, `DEC_TO_BCD`).
  * [`Core/Src/ds1307.c`](file:///Users/harinarayanans/Documents/Oled_display/Core/Src/ds1307.c):
    * `DS1307_Init()`: Checks device responsiveness on I2C address `0x68` (`0xD0`), reads register `0x00`, and clears the Clock Halt (CH) bit 7 to ensure the 32.768 kHz oscillator starts.
    * `DS1307_GetTime()`: Performs burst I2C read of all 7 timekeeper registers (`0x00`–`0x06`) and unpacks BCD to integers.
    * `DS1307_SetTime()`: Packs integers into BCD format and writes all 7 registers in a single burst transaction.

---

### 3. Firmware Integration in `Core/Src/main.c`
* **Removal of Simulated Time**:
  * Removed simulated variables `simHour`, `simMinute`, `simSecond`, `simDay`.
  * Added `RTC_DateTime_t currentRtc` and `bool rtcAvailable`.
* **Super-Loop Polling**:
  * Added non-blocking RTC read every 500 ms (`now - lastRtcRead >= 500`).
  * Added visual alert indicator (`!`) on screen header if RTC communication is lost.
* **Alarm Evaluation**:
  * Updated Dose 1 and Dose 2 alarms to compare against real `currentRtc.hours`, `currentRtc.minutes`, and `currentRtc.date`.
* **Expansion of 3-Button Setup Menu**:
  * Redesigned `SETUP_MENU` to support 3 distinct items:
    1. `PILL 1 TIME`
    2. `PILL 2 TIME`
    3. `ADJUST CLOCK`
  * Added logic in `SETUP_CONFIRM` to write updated time directly to the DS1307 RTC via `DS1307_SetTime()` when option 3 is selected.
  * Added exit capability using Button 3 (`PB15`) from the menu.
* **Header Comments Update**:
  * Updated top-level pinout configuration comment in `Core/Src/main.c` to explicitly document the shared I2C bus with the DS1307 RTC (`PB6`/`PB7`), VCC/GND lines, and expanded button roles.

---

### 4. Documentation & Evaluator Package Generation
* **Documentation Folder Created**: [`docs/`](file:///Users/harinarayanans/Documents/Oled_display/docs/)
  * `01_project_overview.md`: High-level system architecture, wiring table, and state machine.
  * `02_stm32_architecture.md`: In-depth ARM Cortex-M3 specifications, clock tree (72 MHz), wait states, and peripherals.
  * `03_stlink_swd_debugging.md`: ST-LINK v2 programming, SWD vs JTAG comparison, boot jumpers, and live debugging.
  * `04_oled_ssd1306_driver.md`: SSD1306 GDDRAM page layout, framebuffer equations, and Bresenham rendering.
  * `05_ds1307_rtc_subsystem.md`: DS1307 register mapping, BCD arithmetic, 32.768 kHz crystal theory, and I2C sharing.
  * `06_sd_card_logging_roadmap.md`: SD card interface over SPI, FatFS integration plan, and CSV schema.
  * `07_viva_evaluator_qa.md`: 10 comprehensive viva voce questions with detailed technical answers.
  * `AGENT_LOG.md`: Complete audit trail of all code and architecture changes.

---

## Session Date: 2026-11-02

### 1. Diagnostic I2C Scanner & Software Fallback Clock
* Added an I2C scan loop upon startup that probes 7-bit addresses `0x01` through `0x7F` and renders detected addresses directly on the OLED.
* Added non-blocking tick-based software clock fallback in the main super-loop so that testing and simulation can proceed uninterrupted if the physical RTC module is not connected.

### 2. MicroSD Card Subsystem Implementation (SPI1)
* **Files Created**:
  * [`Core/Inc/sd_card.h`](file:///Users/harinarayanans/Documents/MC_project_Pill_Box/Core/Inc/sd_card.h): Defines SPI pins, log events (`DOSE_1_TAKEN`, `DOSE_2_TAKEN`, `WRONG_BUTTON`, `CLOCK_ADJUST`, `CONFIG_UPDATE`), and public APIs.
  * [`Core/Src/sd_card.c`](file:///Users/harinarayanans/Documents/MC_project_Pill_Box/Core/Src/sd_card.c): Full SPI driver implementation using direct register manipulation on SPI1 (`PA4` CS, `PA5` SCK, `PA6` MISO, `PA7` MOSI), standard SD initialization sequence (CMD0, CMD8, ACMD41), persistent config store/load, and CSV format event logging.
* **Firmware Integration in `Core/Src/main.c`**:
  * Added `sdAvailable` detection during boot.
  * Added automatic loading of alarm times from `SD_LoadConfig()` at startup.
  * Added persistent save via `SD_SaveConfig()` when Pill 1 or Pill 2 schedules are altered via the setup menu.
  * Added event logging on `SD_LogEvent()` for correct intake confirmation, wrong button tampering, and RTC clock re-calibrations.
  * Updated OLED boot splash screen to report both `RTC: OK/NONE` and `SD: OK/NONE`.


