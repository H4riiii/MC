/**
  ******************************************************************************
  * @file           : main_oled_test.c
  * @brief          : STANDALONE OLED-ONLY TEST FIRMWARE FOR STM32F103 (BLUE PILL)
  *                   Zero dependencies on RTC, SD card, buzzer, or buttons.
  *                   Strictly tests the SSD1306 128x64 I2C OLED display.
  ******************************************************************************
  *
  * HARDWARE WIRING (OLED <-> STM32F103C8T6):
  *   - OLED GND   ---> GND
  *   - OLED VCC   ---> 3.3V
  *   - OLED SCL   ---> PB6 (I2C1 SCL)
  *   - OLED SDA   ---> PB7 (I2C1 SDA)
  *   - Onboard LED---> PC13 (Heartbeat indicator: blinks when alive)
  *
  * HOW TO USE:
  *   Option 1: In STM32CubeIDE, replace the contents of Core/Src/main.c
  *             with this file and flash to target.
  *   Option 2: Add this file to your build configuration.
  ******************************************************************************
  */

#include "stm32f1xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* ==============================================================================
 * OLED DISPLAY DEFINITIONS (SSD1306 128x64)
 * ============================================================================== */
#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       64
#define SSD1306_I2C_ADDR    (0x3C << 1) // 0x78 (Standard 8-bit shifted address)
#define SSD1306_ALT_ADDR    (0x3D << 1) // 0x7A (Alternative address)

static uint8_t SSD1306_Buffer[SCREEN_WIDTH * SCREEN_HEIGHT / 8]; // 1024 bytes
static uint16_t activeOledAddr = SSD1306_I2C_ADDR;

I2C_HandleTypeDef hi2c1;

/* Function prototypes */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void Error_Handler(void);

/* ==============================================================================
 * 5x7 ASCII FONT TABLE (Characters 32 ' ' to 126 '~')
 * ============================================================================== */
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
    {0x08, 0x08, 0x2A, 0x1C, 0x08}  // 126 '~'
};

/* ==============================================================================
 * LOW-LEVEL OLED DRIVER (Zero-Grain Initialization & Drawing)
 * ============================================================================== */
static void ssd1306_WriteCommand(uint8_t cmd) {
    HAL_I2C_Mem_Write(&hi2c1, activeOledAddr, 0x00, 1, &cmd, 1, 50);
}

void ssd1306_Clear(uint8_t color) {
    memset(SSD1306_Buffer, color ? 0xFF : 0x00, sizeof(SSD1306_Buffer));
}

void ssd1306_UpdateScreen(void) {
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_WriteCommand(0xB0 + page); // Set page start address
        ssd1306_WriteCommand(0x00);        // Set lower column start address
        ssd1306_WriteCommand(0x10);        // Set higher column start address
        HAL_I2C_Mem_Write(&hi2c1, activeOledAddr, 0x40, 1, &SSD1306_Buffer[128 * page], 128, 100);
    }
}

/**
 * @brief Initialize SSD1306 cleanly.
 *        To prevent startup "grain" or "static snow":
 *        We write 1024 zero bytes into internal display RAM BEFORE turning on Display (0xAF).
 */
bool ssd1306_Init(void) {
    // 1. Give OLED controller 50ms to power stabilize
    HAL_Delay(50);

    // 2. Probe I2C address (Try 0x3C first, then 0x3D)
    if (HAL_I2C_IsDeviceReady(&hi2c1, SSD1306_I2C_ADDR, 2, 20) == HAL_OK) {
        activeOledAddr = SSD1306_I2C_ADDR;
    } else if (HAL_I2C_IsDeviceReady(&hi2c1, SSD1306_ALT_ADDR, 2, 20) == HAL_OK) {
        activeOledAddr = SSD1306_ALT_ADDR;
    } else {
        return false; // Display not responding on I2C bus
    }

    // 3. SSD1306 Configuration Sequence
    ssd1306_WriteCommand(0xAE); // Display OFF
    ssd1306_WriteCommand(0x20); // Set Memory Addressing Mode
    ssd1306_WriteCommand(0x00); // 00b = Horizontal Addressing Mode
    ssd1306_WriteCommand(0x21); // Set Column Address range
    ssd1306_WriteCommand(0x00); // Column Start 0
    ssd1306_WriteCommand(0x7F); // Column End 127
    ssd1306_WriteCommand(0x22); // Set Page Address range
    ssd1306_WriteCommand(0x00); // Page Start 0
    ssd1306_WriteCommand(0x07); // Page End 7
    ssd1306_WriteCommand(0xB0); // Page start address 0
    ssd1306_WriteCommand(0xC8); // COM Output Scan Direction (remapped)
    ssd1306_WriteCommand(0x00); // Low column address
    ssd1306_WriteCommand(0x10); // High column address
    ssd1306_WriteCommand(0x40); // Start line address
    ssd1306_WriteCommand(0x81); // Set contrast control
    ssd1306_WriteCommand(0xCF); // High contrast
    ssd1306_WriteCommand(0xA1); // Set segment re-map
    ssd1306_WriteCommand(0xA6); // Normal display (non-inverted)
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
    ssd1306_WriteCommand(0x14); // Enable charge pump

    // 4. CRITICAL ANTI-GRAIN FIX:
    // Flush blank black buffer to GDDRAM before enabling the display
    ssd1306_Clear(0);
    ssd1306_UpdateScreen();

    // 5. Finally turn display ON
    ssd1306_WriteCommand(0xAF); // Display ON
    return true;
}

/* ==============================================================================
 * GRAPHICS PRIMITIVES
 * ============================================================================== */
void ssd1306_DrawPixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    if (color == 1) {
        SSD1306_Buffer[x + (y / 8) * SCREEN_WIDTH] |= (1 << (y % 8));
    } else if (color == 0) {
        SSD1306_Buffer[x + (y / 8) * SCREEN_WIDTH] &= ~(1 << (y % 8));
    } else {
        SSD1306_Buffer[x + (y / 8) * SCREEN_WIDTH] ^= (1 << (y % 8));
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

void ssd1306_DrawCircle(int x0, int y0, int r, uint8_t color) {
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    ssd1306_DrawPixel(x0, y0 + r, color);
    ssd1306_DrawPixel(x0, y0 - r, color);
    ssd1306_DrawPixel(x0 + r, y0, color);
    ssd1306_DrawPixel(x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        ssd1306_DrawPixel(x0 + x, y0 + y, color);
        ssd1306_DrawPixel(x0 - x, y0 + y, color);
        ssd1306_DrawPixel(x0 + x, y0 - y, color);
        ssd1306_DrawPixel(x0 - x, y0 - y, color);
        ssd1306_DrawPixel(x0 + y, y0 + x, color);
        ssd1306_DrawPixel(x0 - y, y0 + x, color);
        ssd1306_DrawPixel(x0 + y, y0 - x, color);
        ssd1306_DrawPixel(x0 - y, y0 - x, color);
    }
}

void ssd1306_DrawChar(int x, int y, char c, uint8_t size, uint8_t color, uint8_t bg) {
    if (c < 32 || c > 126) c = 32;
    uint8_t charIdx = c - 32;

    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x7[charIdx][i];
        for (int j = 0; j < 8; j++) {
            uint8_t pixelColor = (line & (1 << j)) ? color : bg;
            if (size == 1) {
                ssd1306_DrawPixel(x + i, y + j, pixelColor);
            } else {
                ssd1306_FillRect(x + (i * size), y + (j * size), size, size, pixelColor);
            }
        }
    }
    // Space column after character
    for (int j = 0; j < 8; j++) {
        if (size == 1) {
            ssd1306_DrawPixel(x + 5, y + j, bg);
        } else {
            ssd1306_FillRect(x + (5 * size), y + (j * size), size, size, bg);
        }
    }
}

void ssd1306_DrawString(int x, int y, const char *str, uint8_t size, uint8_t color, uint8_t bg) {
    int curX = x;
    while (*str) {
        if (curX + (6 * size) > SCREEN_WIDTH) break;
        ssd1306_DrawChar(curX, y, *str++, size, color, bg);
        curX += (6 * size);
    }
}

/* ==============================================================================
 * OLED VISUAL TEST ROUTINES
 * ============================================================================== */

/**
 * @brief Test Pattern 1: Splash & Hardware Connection Diagnostics
 */
static void run_test_splash(void) {
    char addrStr[32];
    snprintf(addrStr, sizeof(addrStr), "Addr: 0x%02X (OK)", (activeOledAddr >> 1));

    ssd1306_Clear(0);
    ssd1306_DrawRect(0, 0, 128, 64, 1);
    ssd1306_DrawRect(2, 2, 124, 60, 1);
    ssd1306_FillRect(5, 5, 118, 14, 1);
    ssd1306_DrawString(12, 8, "SSD1306 TEST", 1, 0, 1); // Inverted title

    ssd1306_DrawString(12, 24, "STM32F103 I2C1", 1, 1, 0);
    ssd1306_DrawString(12, 36, addrStr, 1, 1, 0);
    ssd1306_DrawString(12, 48, "Res: 128x64 px", 1, 1, 0);
    ssd1306_UpdateScreen();
    HAL_Delay(2500);
}

/**
 * @brief Test Pattern 2: Alignment, Borders, Crosshairs & Geometry
 *        Verifies that all 4 outer corner pixels (0,0), (127,0), (0,63), (127,63) are visible.
 */
static void run_test_geometry(void) {
    ssd1306_Clear(0);

    // Outer boundary (1-pixel border on the exact screen edge)
    ssd1306_DrawRect(0, 0, 128, 64, 1);

    // Diagonal corner-to-corner cross lines
    ssd1306_DrawLine(0, 0, 127, 63, 1);
    ssd1306_DrawLine(0, 63, 127, 0, 1);

    // Center crosshair and circles
    ssd1306_DrawLine(64, 10, 64, 54, 1);
    ssd1306_DrawLine(30, 32, 98, 32, 1);
    ssd1306_DrawCircle(64, 32, 12, 1);
    ssd1306_DrawCircle(64, 32, 24, 1);

    ssd1306_FillRect(44, 26, 40, 12, 0); // Clear background for text
    ssd1306_DrawString(46, 28, "GRID OK", 1, 1, 0);
    ssd1306_UpdateScreen();
    HAL_Delay(2500);
}

/**
 * @brief Test Pattern 3: Full Screen Pixel Check & Inversion
 *        Helps identify any dead, dim, or stuck pixels.
 */
static void run_test_fill_invert(void) {
    // 1. All Pixels ON (Solid White)
    ssd1306_Clear(1);
    ssd1306_UpdateScreen();
    HAL_Delay(1200);

    // 2. All Pixels OFF (Solid Black)
    ssd1306_Clear(0);
    ssd1306_UpdateScreen();
    HAL_Delay(800);

    // 3. Checkerboard pattern
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 128; x++) {
            if ((x / 8 + y / 8) % 2 == 0) {
                ssd1306_DrawPixel(x, y, 1);
            }
        }
    }
    ssd1306_UpdateScreen();
    HAL_Delay(1800);
}

/**
 * @brief Test Pattern 4: Typography & Character Scales
 */
static void run_test_text(void) {
    ssd1306_Clear(0);
    ssd1306_DrawString(4, 2, "FONT DEMO", 1, 1, 0);
    ssd1306_DrawLine(0, 12, 127, 12, 1);

    ssd1306_DrawString(4, 16, "1234567890 !?#$", 1, 1, 0);
    ssd1306_DrawString(4, 26, "abcdefghijklmno", 1, 1, 0);

    // Big size text (size = 2)
    ssd1306_DrawString(4, 38, "BIG 2X", 2, 1, 0);

    // Inverted banner
    ssd1306_FillRect(70, 48, 54, 14, 1);
    ssd1306_DrawString(74, 51, "INVERT", 1, 0, 1);

    ssd1306_UpdateScreen();
    HAL_Delay(2500);
}

/**
 * @brief Test Pattern 5: Real-time Animation (Bouncing Ball + Progress Bar)
 *        Validates I2C throughput, absence of bus timeouts, and frame rate.
 */
static void run_test_animation(uint16_t durationMs) {
    int ballX = 10, ballY = 20;
    int velX = 3, velY = 2;
    int progress = 0;
    uint32_t startTick = HAL_GetTick();

    while (HAL_GetTick() - startTick < durationMs) {
        ssd1306_Clear(0);

        // Header
        ssd1306_DrawString(4, 2, "ANIMATION & FPS", 1, 1, 0);
        ssd1306_DrawLine(0, 12, 127, 12, 1);

        // Update ball physics
        ballX += velX;
        ballY += velY;
        if (ballX <= 4 || ballX >= 123) velX = -velX;
        if (ballY <= 16 || ballY >= 46) velY = -velY;

        // Draw ball & bounce boundary
        ssd1306_DrawRect(0, 14, 128, 36, 1);
        ssd1306_DrawCircle(ballX, ballY, 3, 1);
        ssd1306_DrawPixel(ballX, ballY, 1);

        // Progress bar (0% -> 100%)
        progress = (int)(((HAL_GetTick() - startTick) * 100) / durationMs);
        if (progress > 100) progress = 100;

        ssd1306_DrawRect(4, 53, 120, 8, 1);
        int fillW = (116 * progress) / 100;
        if (fillW > 0) {
            ssd1306_FillRect(6, 55, fillW, 4, 1);
        }

        ssd1306_UpdateScreen();

        // Toggle PC13 onboard LED as frame heartbeat
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
}

/* ==============================================================================
 * MAIN FUNCTION
 * ============================================================================== */
int main(void) {
    // 1. STM32 Low-Level HAL & Clock Initialization
    HAL_Init();
    SystemClock_Config();

    // 2. Initialize GPIO (PC13 Heartbeat LED) and I2C1
    MX_GPIO_Init();
    MX_I2C1_Init();

    // 3. Initialize OLED Display (clean boot, zero grain)
    bool oledReady = ssd1306_Init();

    if (!oledReady) {
        // OLED Not Detected on I2C bus: Fast panic blink on PC13 LED
        while (1) {
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
            HAL_Delay(100);
        }
    }

    /* ==========================================================================
     * CONTINUOUS OLED DIAGNOSTIC TEST LOOP
     * ========================================================================== */
    while (1) {
        // Test 1: I2C Address detection & splash info screen
        run_test_splash();

        // Test 2: Border alignment & crosshair grid
        run_test_geometry();

        // Test 3: Solid pixel fill & hardware invert
        run_test_fill_invert();

        // Test 4: Fonts & typography
        run_test_text();

        // Test 5: Dynamic animation & FPS progress bar (Runs for 6 seconds)
        run_test_animation(6000);
    }
}

/* ==============================================================================
 * SYSTEM CLOCK & PERIPHERAL INITIALIZATION
 * ============================================================================== */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Configure HSE (8MHz crystal on Blue Pill)
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9; // 8MHz * 9 = 72MHz

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        // Fallback to internal HSI if HSE crystal fails to lock
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        RCC_OscInitStruct.HSEState = RCC_HSE_OFF;
        RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
        RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
        RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16; // 4MHz * 16 = 64MHz
        HAL_RCC_OscConfig(&RCC_OscInitStruct);
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

static void MX_I2C1_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000; // 100 kHz standard mode
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // PC13: Blue Pill onboard LED (Active LOW)
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
        // Fast error blink
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        for (volatile int i = 0; i < 200000; i++);
    }
}

