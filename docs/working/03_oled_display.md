# 3. OLED display

## The panel
The SSD1306 has 128x64 pixels. Its memory (GDDRAM) is organised as **8 pages**, each 8 pixels tall and 128 columns wide. One byte holds a column of 8 vertical pixels, so a page is 128 bytes and the whole screen is 1024 bytes.

```
page 0:  y = 0..7     128 bytes
page 1:  y = 8..15    128 bytes
...
page 7:  y = 56..63   128 bytes
```
Pixel (x, y) is bit `y % 8` of byte `x` in page `y / 8`.

The OLED cannot be read back over I2C, so you cannot change a single pixel in place. You must send whole bytes.

## I2C writes
Every I2C message begins with a control byte:
- `0x00` means the following byte is a **command**.
- `0x40` means the following bytes are **display data**.

The driver sends commands one at a time with `HAL_I2C_Mem_Write(..., 0x00, ...)` and a page of data with register `0x40`.

## Initialisation (`ssd1306_Init`)
1. Wait 100 ms for the OLED's supply to settle.
2. Probe `0x3C`, then `0x3D`. Returns `false` if neither answers.
3. Send the setup commands: display off, **Horizontal Addressing Mode** (`0x20 0x00`), column/segment direction, contrast `0x7F`, 1/64 multiplex, clock divide, pre-charge, COM pins, VCOMH, and **charge pump on** (`0x8D 0x14`; the panel needs it to light).
4. Display on (`0xAF`).
5. **Self-test:** `0xA5` forces every pixel on for 1.5 s, then `0xA4` returns to normal. All white proves the panel and commands work.
6. Clear the memory, page by page. It holds random data at power-up; that is the "static noise".

## Drawing without a frame buffer
A full frame buffer would be 1 KB of RAM. This driver keeps only **one page (128 bytes)** and draws the screen eight times:

```c
for (page = 0; page < 8; page++) {
    ssd1306_BeginPage(page);   // pick the page, clear the 128-byte buffer
    drawEverything();          // same code every time
    ssd1306_EndPage();         // send those 128 bytes to the OLED
}
```
`ssd1306_DrawPixel` ignores any pixel whose `y / 8` is not the current page, so each pass only fills its own page. Lines, rectangles, text and filled rectangles are all built on `DrawPixel`, so they work unchanged.

Before sending each page, `EndPage` sets the column range `0..127` (`0x21`) and the page range to that page (`0x22`). This puts the write position at column 0 of the right page every time, so a lost byte can never push later data out of place.

Cost: the drawing code runs 8 times per frame. The screens are small, and a redraw only happens when something changes.

## Drawing primitives (`ssd1306.c`)

| Function | Does |
|---|---|
| `ssd1306_Clear(color)` | fills the current page buffer |
| `ssd1306_DrawPixel(x, y, color)` | color 1 = on, 0 = off, 2 = toggle |
| `ssd1306_DrawLine` | Bresenham line |
| `ssd1306_DrawRect` / `FillRect` | outline / filled rectangle |
| `ssd1306_DrawChar` / `DrawString` | 5x7 font in a 6-column cell, scalable by `size` |

Text can draw a background colour; background value 2 means "transparent".

## Screens (`ui_screens.c`)
`UI_RenderScreen(ctx)` takes a `UI_Context_t` (state, time, dose times, history, edit values, blink flags) and draws the screen for that state. It never changes the program's state; `main.c` builds the context and calls it when `updateScreen` is true.
`UI_DrawI2CScanSplash` draws the startup scan result.

The header row (`printHeader`) shows the time centered, `!` at the left if the RTC is not answering, and `SD` at the right if a card was found.
