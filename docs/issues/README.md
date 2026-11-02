# Issues log

Problems found while bringing the Smart Pill Box hardware up, what was changed, and what is still open.

| # | Issue | Status | File |
|---|---|---|---|
| 1 | OLED does not turn on / shows static noise | Fixed (works on hardware) | [01_oled_not_turning_on.md](01_oled_not_turning_on.md) |
| 2 | SD card not detected, test hangs at "initialising" | **Open** | [02_sd_card_not_detected.md](02_sd_card_not_detected.md) |
| 3 | Build errors when using the test file | Fixed | [03_build_problems.md](03_build_problems.md) |

Test firmware that exercises every component: [`test/main_full_test.c`](../../test/main_full_test.c).
