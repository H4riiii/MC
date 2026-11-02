# Issue 1: OLED not turning on / static noise

**Status:** fixed. Confirmed working on the board.

## Symptoms
- Commit `a335f8b`: the OLED turned on but showed static noise.
- Later commits (Page Addressing Mode `0x02`, I2C recovery, hardware reset): the OLED did not turn on at all.

## What was found
- At `a335f8b` the OLED driver lived inside `main.c` and the screen was drawn from a 1 KB RAM frame buffer.
- The noise is the SSD1306's display memory (GDDRAM), which starts with random contents after power-up. It was visible because the display was switched on before the memory was cleared.
- Later "fixes" changed several things at once (addressing mode, contrast `0xCF`, address auto-detect, I2C state resets, a forced I2C peripheral reset), and the display stopped lighting up. The exact cause was never isolated because there was no hardware access during the fix; the init was returned to the `a335f8b` values instead.

## What was changed
Files: `Core/Src/ssd1306.c`, `Core/Inc/ssd1306.h`, `Core/Src/ui_screens.c`, `Core/Src/main.c`.

1. **No frame buffer.** Only a 128-byte buffer for the page being drawn is kept.
   - `ssd1306_BeginPage(p)` starts page `p`, `ssd1306_EndPage()` sends it to the OLED.
   - `ui_screens.c` runs each screen's drawing code once per page (8 times per frame). Drawing calls outside the current page are ignored.
   - `ssd1306_UpdateScreen()` was removed.
2. **Init restored to the `a335f8b` values:** Horizontal Addressing Mode (`0x20 0x00`), contrast `0x7F`, 100 ms I2C timeouts, no I2C state resets.
3. **Pointer drift fixed.** Before every page is sent, the driver sets the column range (`0x21`) and page range (`0x22`), so the write position always starts at column 0 of that page.
4. **Memory cleared after Display ON** (page by page) to remove the power-up noise.
5. **Startup delay of 100 ms** before the first command, and address auto-detect (`0x3C` then `0x3D`).
6. **`ssd1306_Init()` now returns `bool`** (false if no OLED answered on I2C). In `main.c`, LED1 (PB1) is switched on in that case.
7. **Temporary self-test:** right after Display ON the driver sends `0xA5` (all pixels on) for 1.5 s, then `0xA4`. All white proves the panel, charge pump and I2C commands work. Remove this block in `ssd1306_Init` once it is no longer needed.
8. **`MX_I2C1_Init` in `main.c`** no longer force-resets I2C or sets the pins itself. The pins are set by `HAL_I2C_MspInit` in `stm32f1xx_hal_msp.c`, as in `a335f8b`.

## Things to know
- The RTC and OLED share one I2C bus (PB6 SCL, PB7 SDA). That is normal: OLED is `0x3C`, DS1307 is `0x68`. Changing pins was not needed.
- Many DS1307 modules want 5 V and pull SDA/SCL up to 5 V. If the OLED misbehaves, try the OLED alone to rule that out.
- Each frame now runs the drawing code 8 times. This has not been measured; it was judged fast enough at 72 MHz.
