# How the Smart Pill Box works

One explanation per part of the project. Everything here is taken from the source in `Core/` as it is now.

| File | Covers |
|---|---|
| [01_system_overview.md](01_system_overview.md) | Hardware, pins, startup order, the main loop |
| [02_state_machine.md](02_state_machine.md) | Every screen/state and how buttons move between them |
| [03_oled_display.md](03_oled_display.md) | SSD1306 driver, per-page drawing (no frame buffer), fonts, UI screens |
| [04_rtc_ds1307.md](04_rtc_ds1307.md) | Real-time clock, how time is read, the fallback clock |
| [05_buttons_and_alarms.md](05_buttons_and_alarms.md) | Button debounce, LEDs, buzzer |
| [06_sd_card.md](06_sd_card.md) | SPI SD driver, config and log storage |
| [07_dose_history.md](07_dose_history.md) | The 3-day taken/missed tracker |
| [08_test_firmware.md](08_test_firmware.md) | The full hardware test in `test/` |

Known problems are in [../issues](../issues/README.md).
