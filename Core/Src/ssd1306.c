/**
  ******************************************************************************
  * @file    ssd1306.c
  * @brief   SSD1306 128x64 OLED display pure C driver (I2C) implementation
  *          Target: STM32F103C8T6 (Blue Pill)
  ******************************************************************************
  */

#include "ssd1306.h"
#include <string.h>

static I2C_HandleTypeDef *pOledI2c = NULL;
static uint16_t activeOledAddr = SSD1306_I2C_ADDR;

/* No full-frame buffer: only the 128 bytes of the page currently being rendered are held.
 * Drawing calls outside that page are ignored; ssd1306_EndPage() streams the page to GDDRAM. */
static uint8_t pageBuf[SCREEN_WIDTH];
static uint8_t curPage = 0;

/* 5x7 ASCII FONT TABLE (Characters 32 ' ' to 126 '~') */
static const uint8_t font5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
    {0x36, 0x49, 0x56, 0x20, 0x50}, // 38 '&'
    {0x00, 0x08, 0x07, 0x03, 0x00}, // 39 "'"
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 ')'
    {0x2A, 0x1C, 0x7F, 0x1C, 0x2A}, // 42 '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 '+'
    {0x00, 0x80, 0x70, 0x30, 0x00}, // 44 ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
    {0x00, 0x00, 0x60, 0x60, 0x00}, // 46 '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 '1'
    {0x72, 0x49, 0x49, 0x49, 0x46}, // 50 '2'
    {0x21, 0x41, 0x49, 0x4D, 0x33}, // 51 '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x31}, // 54 '6'
    {0x41, 0x21, 0x11, 0x09, 0x07}, // 55 '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
    {0x46, 0x49, 0x49, 0x29, 0x1E}, // 57 '9'
    {0x00, 0x00, 0x14, 0x00, 0x00}, // 58 ':'
    {0x00, 0x40, 0x34, 0x00, 0x00}, // 59 ';'
    {0x00, 0x08, 0x14, 0x22, 0x41}, // 60 '<'
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 '='
    {0x00, 0x41, 0x22, 0x14, 0x08}, // 62 '>'
    {0x02, 0x01, 0x59, 0x09, 0x06}, // 63 '?'
    {0x3E, 0x41, 0x5D, 0x59, 0x4E}, // 64 '@'
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, // 65 'A'
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 'B'
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 'C'
    {0x7F, 0x41, 0x41, 0x41, 0x3E}, // 68 'D'
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 'E'
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 'F'
    {0x3E, 0x41, 0x41, 0x51, 0x73}, // 71 'G'
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 'H'
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 'I'
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 'J'
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 'K'
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 'L'
    {0x7F, 0x02, 0x1C, 0x02, 0x7F}, // 77 'M'
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 'N'
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 'O'
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 'P'
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 'Q'
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 'R'
    {0x26, 0x49, 0x49, 0x49, 0x32}, // 83 'S'
    {0x03, 0x01, 0x7F, 0x01, 0x03}, // 84 'T'
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 'U'
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 'V'
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87 'W'
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 'X'
    {0x03, 0x04, 0x78, 0x04, 0x03}, // 89 'Y'
    {0x61, 0x59, 0x49, 0x4D, 0x43}, // 90 'Z'
    {0x00, 0x7F, 0x41, 0x41, 0x41}, // 91 '['
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 '\'
    {0x00, 0x41, 0x41, 0x41, 0x7F}, // 93 ']'
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94 '^'
    {0x40, 0x40, 0x40, 0x40, 0x40}, // 95 '_'
    {0x00, 0x03, 0x07, 0x08, 0x00}, // 96 '`'
    {0x20, 0x54, 0x54, 0x78, 0x40}, // 97 'a'
    {0x7F, 0x28, 0x44, 0x44, 0x38}, // 98 'b'
    {0x38, 0x44, 0x44, 0x44, 0x28}, // 99 'c'
    {0x38, 0x44, 0x44, 0x28, 0x7F}, // 100 'd'
    {0x38, 0x54, 0x54, 0x54, 0x18}, // 101 'e'
    {0x00, 0x08, 0x7E, 0x09, 0x02}, // 102 'f'
    {0x18, 0xA4, 0xA4, 0x9C, 0x78}, // 103 'g'
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // 104 'h'
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // 105 'i'
    {0x20, 0x40, 0x40, 0x3D, 0x00}, // 106 'j'
    {0x7F, 0x10, 0x28, 0x44, 0x00}, // 107 'k'
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // 108 'l'
    {0x7C, 0x04, 0x78, 0x04, 0x78}, // 109 'm'
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // 110 'n'
    {0x38, 0x44, 0x44, 0x44, 0x38}, // 111 'o'
    {0xFC, 0x18, 0x24, 0x24, 0x18}, // 112 'p'
    {0x18, 0x24, 0x24, 0x18, 0xFC}, // 113 'q'
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // 114 'r'
    {0x48, 0x54, 0x54, 0x54, 0x24}, // 115 's'
    {0x04, 0x04, 0x3F, 0x44, 0x24}, // 116 't'
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 117 'u'
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 118 'v'
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 119 'w'
    {0x44, 0x28, 0x10, 0x28, 0x44}, // 120 'x'
    {0x4C, 0x90, 0x90, 0x90, 0x7C}, // 121 'y'
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // 122 'z'
    {0x00, 0x08, 0x36, 0x41, 0x00}, // 123 '{'
    {0x00, 0x00, 0x77, 0x00, 0x00}, // 124 '|'
    {0x00, 0x41, 0x36, 0x08, 0x00}, // 125 '}'
    {0x02, 0x01, 0x02, 0x04, 0x02}, // 126 '~'
};

static void ssd1306_WriteCommand(uint8_t cmd) {
    if (pOledI2c == NULL) return;
    HAL_I2C_Mem_Write(pOledI2c, activeOledAddr, 0x00, 1, &cmd, 1, 100);
}

/* Point the GDDRAM write cursor at column 0 of the given page (Horizontal Addressing Mode) */
static void ssd1306_SetPage(uint8_t page) {
    ssd1306_WriteCommand(0x21); // Column address range
    ssd1306_WriteCommand(0);
    ssd1306_WriteCommand(SCREEN_WIDTH - 1);
    ssd1306_WriteCommand(0x22); // Page address range
    ssd1306_WriteCommand(page);
    ssd1306_WriteCommand(page);
}

bool ssd1306_Init(I2C_HandleTypeDef *hi2c) {
    pOledI2c = hi2c;
    activeOledAddr = SSD1306_I2C_ADDR;
    HAL_Delay(100); // let the OLED supply settle after power-up

    // Auto-detect 0x3C or 0x3D; report whether anything ACKed
    bool present = true;
    if (HAL_I2C_IsDeviceReady(pOledI2c, SSD1306_I2C_ADDR, 3, 50) != HAL_OK) {
        if (HAL_I2C_IsDeviceReady(pOledI2c, SSD1306_ALT_ADDR, 3, 50) == HAL_OK) {
            activeOledAddr = SSD1306_ALT_ADDR;
        } else {
            present = false;
        }
    }

    ssd1306_WriteCommand(0xAE); // Display OFF
    ssd1306_WriteCommand(0x20); // Set Memory Addressing Mode
    ssd1306_WriteCommand(0x00); // 00b: Horizontal Addressing Mode (same init as a335f8b)
    ssd1306_WriteCommand(0xB0); // Page start address 0
    ssd1306_WriteCommand(0xC8); // COM Output Scan Direction (remapped)
    ssd1306_WriteCommand(0x00); // Low column address
    ssd1306_WriteCommand(0x10); // High column address
    ssd1306_WriteCommand(0x40); // Start line address
    ssd1306_WriteCommand(0x81); // Set contrast control
    ssd1306_WriteCommand(0x7F);
    ssd1306_WriteCommand(0xA1); // Set segment re-map (A0/A1)
    ssd1306_WriteCommand(0xA6); // Normal display
    ssd1306_WriteCommand(0xA8); // Multiplex ratio
    ssd1306_WriteCommand(0x3F); // 1/64 duty
    ssd1306_WriteCommand(0xA4); // Output RAM to display
    ssd1306_WriteCommand(0xD3); // Display offset
    ssd1306_WriteCommand(0x00);
    ssd1306_WriteCommand(0xD5); // Display clock divide ratio/oscillator
    ssd1306_WriteCommand(0x80);
    ssd1306_WriteCommand(0xD9); // Pre-charge period
    ssd1306_WriteCommand(0xF1);
    ssd1306_WriteCommand(0xDA); // COM pins hardware configuration
    ssd1306_WriteCommand(0x12);
    ssd1306_WriteCommand(0xDB); // Vcomh
    ssd1306_WriteCommand(0x40);
    ssd1306_WriteCommand(0x8D); // Charge pump regulator
    ssd1306_WriteCommand(0x14); // Enable charge pump (generates ~7.5V panel boost)

    ssd1306_WriteCommand(0xAF); // Display ON

    // TEMPORARY DIAGNOSTIC: force every pixel on for 1.5 s, independent of GDDRAM contents.
    // All white = panel, charge pump and I2C commands work. Remove once the display is confirmed.
    ssd1306_WriteCommand(0xA5);
    HAL_Delay(1500);
    ssd1306_WriteCommand(0xA4);

    // GDDRAM powers up with random data (the static noise): blank it page by page
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_BeginPage(page);
        ssd1306_EndPage();
    }
    return present;
}

void ssd1306_Clear(uint8_t color) {
    memset(pageBuf, color ? 0xFF : 0x00, sizeof(pageBuf));
}

void ssd1306_BeginPage(uint8_t page) {
    curPage = page;
    ssd1306_Clear(0);
}

void ssd1306_EndPage(void) {
    if (pOledI2c == NULL) return;
    ssd1306_SetPage(curPage);
    HAL_I2C_Mem_Write(pOledI2c, activeOledAddr, 0x40, 1, pageBuf, SCREEN_WIDTH, 100);
}

void ssd1306_DrawPixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    if ((y >> 3) != curPage) return; // belongs to a page that is not being rendered now
    if (color == 1) {
        pageBuf[x] |= (1 << (y & 7));
    } else if (color == 0) {
        pageBuf[x] &= ~(1 << (y & 7));
    } else {
        pageBuf[x] ^= (1 << (y & 7));
    }
}

void ssd1306_DrawLine(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        ssd1306_DrawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ssd1306_DrawRect(int x, int y, int w, int h, uint8_t color) {
    ssd1306_DrawLine(x, y, x + w - 1, y, color);
    ssd1306_DrawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    ssd1306_DrawLine(x, y, x, y + h - 1, color);
    ssd1306_DrawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void ssd1306_FillRect(int x, int y, int w, int h, uint8_t color) {
    for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
            ssd1306_DrawPixel(i, j, color);
        }
    }
}

void ssd1306_DrawChar(int x, int y, char c, uint8_t size, uint8_t color, uint8_t bg) {
    if (c < 32 || c > 126) c = 32;
    uint8_t charIdx = c - 32;

    for (int col = 0; col < 6; col++) {
        uint8_t line = (col < 5) ? font5x7[charIdx][col] : 0x00;
        for (int row = 0; row < 8; row++) {
            uint8_t pixelOn = (line & (1 << row)) ? color : bg;
            if (pixelOn != 2) { // 2 = transparent
                if (size == 1) {
                    ssd1306_DrawPixel(x + col, y + row, pixelOn);
                } else {
                    for (int si = 0; si < size; si++) {
                        for (int sj = 0; sj < size; sj++) {
                            ssd1306_DrawPixel(x + col * size + si, y + row * size + sj, pixelOn);
                        }
                    }
                }
            }
        }
    }
}

void ssd1306_DrawString(int x, int y, const char *str, uint8_t size, uint8_t color, uint8_t bg) {
    while (*str) {
        ssd1306_DrawChar(x, y, *str, size, color, bg);
        x += 6 * size;
        str++;
    }
}

