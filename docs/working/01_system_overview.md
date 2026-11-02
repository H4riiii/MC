# 1. System overview

## What it is
A pill reminder on an STM32F103C8T6 (Blue Pill). At two set times a day it flashes the screen and LED, sounds a buzzer, and waits for you to press the button that matches the pill. It records what was taken.

## Hardware and pins

| Part | Pins | Bus |
|---|---|---|
| SSD1306 OLED 128x64 | PB6 (SCL), PB7 (SDA) | I2C1, address `0x3C` |
| DS1307 real-time clock | PB6, PB7 (same bus) | I2C1, address `0x68` |
| MicroSD module | PA4 CS, PA5 SCK, PA6 MISO, PA7 MOSI | SPI1 |
| Button 1 / 2 / 3 | PB12 / PB14 / PB15 | input, pull-up, active low |
| LED 1 / LED 2 | PB1 / PB10 | output |
| Buzzer | PB0 | output |

The OLED and RTC share one bus; each answers only to its own address, so this works.

## Source layout

| File | Job |
|---|---|
| `Core/Src/main.c` | startup, main loop, state machine, clock config |
| `Core/Src/ssd1306.c` | low-level OLED driver, font, drawing primitives |
| `Core/Src/ui_screens.c` | what each screen looks like |
| `Core/Src/ds1307.c` | RTC driver |
| `Core/Src/buttons.c` | button reading and debounce |
| `Core/Src/alarms.c` | LEDs and buzzer |
| `Core/Src/sd_card.c` | SD driver, config/log storage, 3-day history |
| `Core/Src/stm32f1xx_hal_msp.c` | sets up the I2C pins for the HAL |

## Clock
`SystemClock_Config` runs the CPU at 72 MHz from the 8 MHz crystal (x9 through the PLL). If the crystal does not start it falls back to the internal oscillator (about 64 MHz). `HAL_Delay` and `HAL_GetTick` count milliseconds from SysTick.

## Startup order (`main()`)
1. `HAL_Init`, `SystemClock_Config`.
2. `MX_I2C1_Init` (100 kHz), `Alarms_Init`, `Buttons_Init`.
3. `ssd1306_Init`: wait 100 ms, find the OLED, send the setup commands, switch on, run the all-pixels self-test, clear the screen.
4. `DS1307_Init`; if it answers, read the current time.
5. `SD_Init`; if a card is found, `SD_LoadConfig` loads the two dose times.
6. Scan the I2C bus (addresses 1 to 127) and show the result with the RTC and SD status for 2.5 s.
7. `DoseHistory_Init`, then enter the loop in state `NORMAL_IDLE`.

## Main loop
The program is one endless loop (no operating system). Each pass, about every 10 ms:
1. **Every 500 ms:** read the RTC (or advance the fallback clock), update the 3-day history, and mark the idle screen for redraw when the seconds changed.
2. **Read buttons** (`Buttons_Scan`), which reports a press only once per press.
3. **Blink the cursor** while a time is being edited.
4. **Run the state machine** (see [02_state_machine.md](02_state_machine.md)).
5. **Redraw the OLED** only if something changed (`updateScreen`).
6. `HAL_Delay(10)`.

Redrawing only on change keeps the loop from spending its time sending pixels over I2C.
