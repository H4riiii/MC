# OLED Buffering Fix & Performance Optimization Context

Target: **STM32F103C8T6 (Blue Pill)**  
Peripherals: **SSD1306 (128x64 OLED) & DS1307 RTC on shared I2C1 (PB6/PB7)**  
Status: **IMPLEMENTED ✅**

---

## 1. Problem Statement
The integrated Smart Pill Box firmware suffered from display "buffing" (screen tearing, frame lag, button sluggishness, and periodic flicker).
In contrast, the isolated test code in `test/main_oled_test.c` ran at high frame rates with zero lag.

---

## 2. Root Cause Analysis

### Cause A: Unconditional Periodic Redraws (105 ms bus block every 500 ms)
* In `Core/Src/main.c`, the super-loop RTC polling block executed `updateScreen = true` every 500 ms unconditionally.
* Transferring a complete 128×64 frame (1024 bytes GDDRAM across 8 pages) over 100 kHz I2C takes **~105 ms**.
* In `NORMAL_IDLE`, this meant wiping the screen twice per second even if the second hadn't changed.
* In menus (`SETUP_MENU`, `SETUP_EDIT_TIME`, `VIEW_LOGS`), the clock second is not even shown, yet the menu was wiped and retransmitted every 500 ms, causing severe button lag and noticeable screen strobing.

### Cause B: 100 ms Blocking Timeout on Shared I2C Bus
* `Core/Src/ds1307.c` used a `100 ms` timeout for `HAL_I2C_Mem_Read()` and `HAL_I2C_Mem_Write()`.
* Reading 7 timekeeper bytes at 100 kHz takes **~1.1 ms**.
* When the RTC is unseated, slow, or unpowered, the MCU halted for 100 ms waiting for a NACK/timeout before giving control back to the OLED driver.

---

## 3. The 2-Part Fix Plan

### Part 1: Smart Conditional Redraw (Fix 1)
* Track `lastDisplayedSecond` in `main.c`.
* In `NORMAL_IDLE`, only set `updateScreen = true` when `currentRtc.seconds != lastDisplayedSecond`.
* While inside any setup or log viewing menu, never redraw on RTC ticks. Redraws are strictly event-driven:
  * When a button is pressed (`btn1Pressed || btn2Pressed || btn3Pressed`).
  * When the cursor blink timer toggles (`FIELD_BLINK_INTERVAL`).
  * When an alarm toggles (`currentBlinkInterval`).

### Part 2: Low-Latency 10 ms RTC Timeout (Fix 2)
* In `Core/Src/ds1307.c`, change all `HAL_I2C_Mem_Read/Write` timeouts from `100` to `10` ms.
* 10 ms provides a 900% safety margin over the 1.1 ms physical packet time, but completely eliminates stalling if the RTC fails to respond.

---

## 4. Implementation Task List

1. **`Core/Src/ds1307.c`**:
   - [x] Change timeout parameters in `DS1307_Init()`, `DS1307_GetTime()`, `DS1307_SetTime()` to `10`.
2. **`Core/Src/main.c`**:
   - [x] Add `static uint8_t lastDisplayedSecond = 0xFF;`.
   - [x] In the 500 ms RTC timer block, only trigger `updateScreen = true` if `currentState == NORMAL_IDLE` AND `currentRtc.seconds != lastDisplayedSecond`.
   - [x] Update `lastDisplayedSecond = currentRtc.seconds`.
3. **Verification**:
   - [ ] Verify zero flicker on idle clock.
   - [ ] Verify zero menu lag when scrolling through settings.
   - [ ] Verify smooth operation even if RTC is unattached.

