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

### 3. Automated Documentation PDF Generator & Maintenance Rule
* **MANDATE FOR ALL FUTURE AGENTS**:
  * Whenever any documentation file `docs/*.md` (except `AGENT_LOG.md`) is created or modified, the agent MUST immediately regenerate and update the matching PDF files in `docs/` by running:
    ```bash
    python3 docs/generate_pdfs.py
    ```
  * `generate_pdfs.py` parses headings, tables, code blocks, alerts, and formatting into clean printable PDF documents:
    * `01_project_overview.pdf`
    * `02_stm32_architecture.pdf`
    * `03_stlink_swd_debugging.pdf`
    * `04_oled_ssd1306_driver.pdf`
    * `05_ds1307_rtc_subsystem.pdf`
    * `06_sd_card_logging_roadmap.pdf`
    * `07_viva_evaluator_qa.pdf`




---

### 4. Implementation of On-OLED 3-Day Dosage Viewer & Sector Persistence
* **On-OLED 3-Day Dosage Viewer**:
  * Added `> 4. VIEW LOGS` option to the main Settings menu.
  * Created `VIEW_LOGS` screen showing 1 page per day:
    * Page 1: Today (`LOG [1/3] DD/MM (TODAY)`) with Pill 1 & Pill 2 status (`TAKEN`, `PENDING`, or `MISSED`).
    * Page 2: Yesterday (`LOG [2/3] DD/MM (-1 DAY)`).
    * Page 3: Day Before Yesterday (`LOG [3/3] DD/MM (-2 DAYS)`).
  * Added button navigation: Button 1 (`PB12`) cycles through the 3 days; Button 3 (`PB15`) returns to the menu.
  * Implemented calendar math taking into account month boundaries and leap years.
* **Low-Level SPI Flash Sector Persistence**:
  * Implemented real `CMD17` (single-block read) and `CMD24` (single-block write) routines in `Core/Src/sd_card.c`.
  * **Sector 1000**: Stores persistent configuration `PILLBOX_CFG_V1 P1=HH:MM P2=HH:MM`. Successfully loaded on boot via `SD_LoadConfig()` and updated on save via `SD_SaveConfig()`.
  * **Sector 1001+**: Stores audit log records with timestamps and event details.
* **Electronics Deep-Dive Documentation Upgrade**:
  * Completely updated all 7 `.md` files in `docs/` with deep electrical engineering physics, circuit equations, transistor models (open-drain vs push-pull, 5V tolerance silicon layout, RC pull-up rise times, crystal load capacitance, power-fail comparator logic), and structured viva voce answers tailored for CSE students facing EC evaluators.
  * Regenerated all 7 corresponding PDF files using `generate_pdfs.py`.

---

## Session Date: 2026-11-03

### 1. Dedicated OLED Hardware Diagnostics Suite (`test/`)
* Created isolated standalone diagnostic firmware `test/main_oled_test.c` with zero peripheral dependencies.
* Added auto-detection for I2C addresses `0x3C`/`0x3D`, clean zero-grain GDDRAM pre-flush before `0xAF` Display ON, alignment test patterns, typography verification, and real-time animation tests.

### 2. Architectural Modularization of `Core/Src/main.c`
* Split monolithic 1129-line `main.c` into single-responsibility hardware abstraction modules:
  * `Core/Inc/ssd1306.h` & `Core/Src/ssd1306.c`: Pure C SSD1306 I2C driver, font table, and graphics primitives.
  * `Core/Inc/ui_screens.h` & `Core/Src/ui_screens.c`: FSM screen layout renderer (`NORMAL_IDLE`, reminders, menus, time editing, 3-day history).
  * `Core/Inc/buttons.h` & `Core/Src/buttons.c`: Edge-detection debouncer for PB12, PB14, PB15.
  * `Core/Inc/alarms.h` & `Core/Src/alarms.c`: GPIO indicator drivers for Pill 1 LED (PB1), Pill 2 LED (PB10), and Buzzer (PB0).
  * Reduced `main.c` down to a clean, readable orchestrator.

### 3. OLED Buffering Root-Cause Diagnosis & Fix Strategy
* Identified root causes of screen "buffering" / flickering:
  * Unconditional 500 ms redraw loop causing ~105 ms I2C transmission freezes.
  * 100 ms blocking timeouts on DS1307 I2C reads stalling the super-loop.
* Created fix implementation plan `docs/OLED_BUFFER_FIX_PLAN.md`:
  * **Fix 1**: Smart conditional redraw (only update on actual second change in idle, pure event-driven in menus).
  * **Fix 2**: Reduce RTC timeouts from 100 ms to 10 ms.

---

## Session Date: 2026-11-03

### Fix 1 — Smart Conditional Redraw (`Core/Src/main.c`)

* **Problem:** `updateScreen = true` was unconditionally set at the end of the 500 ms RTC poll block, forcing a full ~105 ms I2C OLED frame transfer twice per second even while in menus (where the second digit is never displayed).
* **Fix:** Added `static uint8_t lastDisplayedSecond = 0xFF` tracking variable. The 500 ms block now only sets `updateScreen = true` when **both** of these are true: `currentState == NORMAL_IDLE` **and** `currentRtc.seconds != lastDisplayedSecond`. All menu and alarm states are already event-driven (button presses + blink timer), so they never need the 500 ms RTC timer to trigger a redraw.
* **State transition resets:** Added `lastDisplayedSecond = 0xFF` alongside every `currentState = NORMAL_IDLE` assignment that represents a transition *back* from a menu/alarm (`DOSE_CONFIRMED`, `SETUP_FEEDBACK`, `SETUP_MENU` Btn3 exit). This ensures the clock face is immediately redrawn once on return without waiting up to 500 ms for the next RTC tick.
* **No functional change** to alarms, dose history, SD logging, or menu navigation.

### Fix 2 — DS1307 I2C Timeout Reduction (`Core/Src/ds1307.c`)

* **Problem:** All `HAL_I2C_Mem_Read` / `HAL_I2C_Mem_Write` calls used 100 ms timeouts. A disconnected or stalled RTC could block the main loop for 100 ms × (up to 6 calls in `DS1307_Init`) = 600 ms per detection cycle.
* **Fix:** Changed ALL I2C timeout parameters from `100` ms to `10` ms across:
  * `DS1307_Init()` — 1× `Mem_Read` + 4× conditional `Mem_Write` calls
  * `DS1307_GetTime()` — 1× `Mem_Read`
  * `DS1307_SetTime()` — 1× `Mem_Write`
* **Rationale:** A 7-byte I2C read at 100 kHz SCL takes ~1.1 ms. 10 ms provides a 9× safety margin while capping worst-case RTC stall from 100 ms to 10 ms per call.
* **Note:** `HAL_I2C_IsDeviceReady` timeout was already changed to 10 ms in a prior session.
* **No functional change** to time-keeping logic or alarm triggers.

### Fix 3 — SSD1306 Page Addressing Mode & Hardware Erratum Recovery (`Core/Src/ssd1306.c`, `Core/Src/main.c`)

* **Problem:** Setting SSD1306 memory addressing mode to Horizontal (`0x00`) while sending per-page commands (`0xB0 + page`) caused pointer desynchronization whenever an I2C glitch, arbitration delay, or dropped byte occurred. The pointer offset accumulated frame after frame until the entire screen turned into static noise. Furthermore, STM32F103 hardware I2C silicon lockup occurred on reset if the bus lines were low.
* **Fix:**
  1. Configured SSD1306 to Page Addressing Mode (`0x02`) in `ssd1306_Init()`, ensuring every page transmission (`0xB0 + page`, `0x00`, `0x10`) explicitly resets column and page pointers.
  2. Moved `0xAF` (Display ON) to execute directly after enabling the charge pump (`0x8D 0x14`) with a 50 ms stabilization delay, preventing sleep mode freezes.
  3. Added I2C error recovery in `ssd1306_WriteCommand()` and `ssd1306_UpdateScreen()` to reset HAL `ErrorCode` and `State` upon intermittent dropouts.
  4. Added `__HAL_RCC_I2C1_FORCE_RESET()` and internal pull-ups (`GPIO_PULLUP`) on PB6/PB7 in `MX_I2C1_Init()`.
  5. Implemented visual panic blink on Pill 1 LED (`PB1`) in `Error_Handler()`.
