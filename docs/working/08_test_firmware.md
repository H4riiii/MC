# 8. Hardware test firmware

File: `test/main_full_test.c`. It replaces `Core/Src/main.c` while testing; it uses the same drivers. Restore the real `main.c` afterwards, and never commit the test as `main.c`.

## What it runs, in order
1. **OLED:** the all-pixels-on flash from `ssd1306_Init`, then a message screen. If the OLED does not answer on I2C, LED 1 stays on and the test stops.
2. **I2C scan:** lists addresses that answer (`3C` OLED, `68` RTC).
3. **RTC:** reads the time twice 1.3 s apart; passes if the seconds changed.
4. **SD card:** initialises the card, writes a test pattern to sector 1010, reads it back and compares, then restores the old contents of that sector. On failure it shows `stage`, `resp` and `spi timeouts`.
5. **LEDs and buzzer:** LED 1 with a beep, then LED 2 with a beep. Press B1 if you saw and heard them, B3 if not.
6. **Buttons:** asks for B1, B2, B3 in turn (10 s each).
7. **Summary** page with OK/FAIL per item, then a **live** page.

The live page shows the RTC time and `#` for each held button. Holding B1 lights LED 1, B2 lights LED 2, B3 sounds the buzzer.

## Switches
`#define RUN_SD_TEST 0` skips the SD step (useful while the card is being fixed).

## How it draws
It uses the same one-page-at-a-time method as the main program: `renderFrame(fn)` runs a drawing function once for each of the 8 pages.

## Build notes
- The SD diagnostic variables are declared weak in the test so it builds with either version of `sd_card.c/.h`.
- The test does not depend on the return value of `ssd1306_Init`; it probes the OLED address itself.
- The LED and buzzer results depend on you pressing a button; the code cannot tell if they actually worked.
