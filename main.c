/*
 * ==============================================================================
 * SMART PILL BOX - STM32 C FIRMWARE (HAL & SSD1306 PURE C DRIVER)
 * Target: STM32F103C8T6 (Blue Pill)
 *
 * PINOUT CONFIGURATION:
 * -------------------------------------------------------------
 * OLED SCL      -> PB6 (I2C1 SCL)
 * OLED SDA      -> PB7 (I2C1 SDA)
 * OLED VCC      -> 3.3V
 * OLED GND      -> GND
 *
 * Button 1      -> PB12 (Terminal 1 to PB12, Terminal 2 to GND) - Dose 1 / Menu +
 * Button 2      -> PB14 (Terminal 1 to PB14, Terminal 2 to GND) - Dose 2 / Select
 * Button 3      -> PB15 (Terminal 1 to PB15, Terminal 2 to GND) - Setup Menu
 *
 * Optional:
 * LED 1 (Pill 1)-> PB1  (Active HIGH via resistor) [ENABLE_LEDS = 0 by default]
 * LED 2 (Pill 2)-> PB10 (Active HIGH via resistor) [ENABLE_LEDS = 0 by default]
 * Buzzer        -> PB0  (Piezo buzzer)             [ENABLE_BUZZER = 0 by default]
 * ==============================================================================
 */

#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ==============================================================================
 * HARDWARE CONFIGURATION & PIN DEFINITIONS
 * ============================================================================== */

// Feature flags
#define ENABLE_LEDS       0   // 1 = Enable LEDs on PB1 (Pill 1) and PB10 (Pill 2)
#define ENABLE_BUZZER     0   // 1 = Enable Buzzer on PB0

// OLED I2C Display (128x64 SSD1306)
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define SSD1306_I2C_ADDR  (0x3C << 1) // 0x78: 8-bit shifted I2C address for STM32 HAL

// Pushbuttons (Active LOW with internal pull-up)
#define BUTTON_PORT       GPIOB
#define BUTTON_1_PIN      GPIO_PIN_12 // PB12: Pill 1 Dose confirm / Menu +
#define BUTTON_2_PIN      GPIO_PIN_14 // PB14: Pill 2 Dose confirm / Menu select
#define BUTTON_3_PIN      GPIO_PIN_15 // PB15: Open Setup Menu

#if ENABLE_LEDS
#define LED_PORT          GPIOB
#define LED_1_PIN         GPIO_PIN_1  // PB1: Pill 1 LED
#define LED_2_PIN         GPIO_PIN_10 // PB10: Pill 2 LED
#endif

#if ENABLE_BUZZER
#define BUZZER_PORT       GPIOB
#define BUZZER_PIN        GPIO_PIN_0  // PB0: Buzzer
#endif

/* ==============================================================================
 * TIMING CONSTANTS (Milliseconds)
 * ============================================================================== */
#define DEBOUNCE_TIME          200
#define NORMAL_BLINK_INTERVAL  500
#define FAST_BLINK_INTERVAL    150
#define FIELD_BLINK_INTERVAL   400

/* ==============================================================================
 * SYSTEM STATES & DATA STRUCTURES
 * ============================================================================== */
typedef enum {
    NORMAL_IDLE,
    REMINDER_DOSE_1,
    REMINDER_DOSE_2,
    DOSE_CONFIRMED,
    SETUP_MENU,
    SETUP_EDIT_HOUR,
    SETUP_EDIT_MINUTE,
    SETUP_CONFIRM,
    SETUP_FEEDBACK
} SystemState_t;

typedef struct {
    int hour;
    int minute;
    int lastTriggeredDay;
} DoseAlarm_t;

// Global System Variables
static SystemState_t currentState = NORMAL_IDLE;
static bool updateScreen = true;

// Scheduled dose alarms: Dose 1 at 07:30, Dose 2 at 19:00
static DoseAlarm_t dose1 = {7, 30, -1};
static DoseAlarm_t dose2 = {19, 0, -1};

// Setup temporary variables
static int setupSelectedPill = 1;
static int tempHour = 0;
static int tempMinute = 0;
static bool confirmSaveChoice = true;
static uint32_t feedbackTimer = 0;
static uint32_t doseConfirmedTimer = 0;

// Button debounce tracking
static GPIO_PinState lastButton1State = GPIO_PIN_SET;
static GPIO_PinState lastButton2State = GPIO_PIN_SET;
static GPIO_PinState lastButton3State = GPIO_PIN_SET;

static uint32_t lastButton1Press = 0;
static uint32_t lastButton2Press = 0;
static uint32_t lastButton3Press = 0;

// Alarm & UI tracking
static uint32_t lastAlarmToggle = 0;
static uint32_t currentBlinkInterval = NORMAL_BLINK_INTERVAL;
static bool alarmVisualState = false;
static bool wrongButtonPressed = false;

static uint32_t lastBlinkToggle = 0;
static uint32_t lastTimeUpdate = 0;
static bool cursorVisible = true;

// Simulated clock (24h) - Starts at 07:28:00
static int simHour   = 7;
static int simMinute = 28;
static int simSecond = 0;
static int simDay    = 1;

/* ==============================================================================
 * PERIPHERAL HANDLERS & FORWARD DECLARATIONS
 * ============================================================================== */
I2C_HandleTypeDef hi2c1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void Error_Handler(void);

/* ==============================================================================
 * EMBEDDED SSD1306 PURE C OLED DRIVER & FONT
 * ============================================================================== */
static uint8_t SSD1306_Buffer[SCREEN_WIDTH * SCREEN_HEIGHT / 8];

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
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 backslash
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
    HAL_I2C_Mem_Write(&hi2c1, SSD1306_I2C_ADDR, 0x00, 1, &cmd, 1, 100);
}

void ssd1306_Init(void) {
    HAL_Delay(100);
    ssd1306_WriteCommand(0xAE); // Display OFF
    ssd1306_WriteCommand(0x20); // Set Memory Addressing Mode
    ssd1306_WriteCommand(0x00); // 00b: Horizontal Addressing Mode
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
    ssd1306_WriteCommand(0x14); // Enable charge pump
    ssd1306_WriteCommand(0xAF); // Display ON
}

void ssd1306_Clear(uint8_t color) {
    memset(SSD1306_Buffer, color ? 0xFF : 0x00, sizeof(SSD1306_Buffer));
}

void ssd1306_UpdateScreen(void) {
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_WriteCommand(0xB0 + page);
        ssd1306_WriteCommand(0x00);
        ssd1306_WriteCommand(0x10);
        HAL_I2C_Mem_Write(&hi2c1, SSD1306_I2C_ADDR, 0x40, 1, &SSD1306_Buffer[128 * page], 128, 100);
    }
}

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

/* ==============================================================================
 * OLED UI RENDERING
 * ============================================================================== */

static void printHeader(void) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", simHour, simMinute, simSecond);
    ssd1306_DrawString(38, 2, buf, 1, 1, 0);
    ssd1306_DrawLine(0, 12, 127, 12, 1);
}

static void renderScreen(void) {
    ssd1306_Clear(0);

    switch (currentState) {
        case NORMAL_IDLE: {
            printHeader();
            char buf[16];
            ssd1306_DrawString(4, 18, "P1: ", 1, 1, 0);
            snprintf(buf, sizeof(buf), "%02d:%02d", dose1.hour, dose1.minute);
            ssd1306_DrawString(28, 18, buf, 1, 1, 0);

            ssd1306_DrawString(68, 18, "P2: ", 1, 1, 0);
            snprintf(buf, sizeof(buf), "%02d:%02d", dose2.hour, dose2.minute);
            ssd1306_DrawString(92, 18, buf, 1, 1, 0);

            ssd1306_DrawLine(0, 30, 127, 30, 1);
            ssd1306_DrawString(12, 36, "READY & MONITORING", 1, 1, 0);
            ssd1306_DrawString(10, 52, "Press [B3] for Setup", 1, 1, 0);
            break;
        }

        case REMINDER_DOSE_1:
        case REMINDER_DOSE_2: {
            printHeader();
            bool isDose1 = (currentState == REMINDER_DOSE_1);

            if (wrongButtonPressed) {
                ssd1306_FillRect(0, 16, 128, 48, alarmVisualState ? 1 : 0);
                uint8_t fg = alarmVisualState ? 0 : 1;
                uint8_t bg = alarmVisualState ? 1 : 0;

                ssd1306_DrawString(10, 20, "WRONG PILL!", 2, fg, bg);
                ssd1306_DrawString(10, 40, isDose1 ? "Press BUTTON 1" : "Press BUTTON 2", 1, fg, bg);
                ssd1306_DrawString(10, 52, isDose1 ? "for PILL 1" : "for PILL 2", 1, fg, bg);
            } else {
                if (alarmVisualState) {
                    ssd1306_FillRect(0, 15, 128, 18, 1);
                    ssd1306_DrawString(12, 17, "MEDICINE!", 2, 0, 1);
                } else {
                    ssd1306_DrawString(12, 17, "MEDICINE!", 2, 1, 0);
                }
                ssd1306_DrawString(8, 38, isDose1 ? "TAKE: >> PILL 1 <<" : "TAKE: >> PILL 2 <<", 1, 1, 0);
                ssd1306_DrawString(8, 52, isDose1 ? "Press Button 1 to OK" : "Press Button 2 to OK", 1, 1, 0);
            }
            break;
        }

        case DOSE_CONFIRMED: {
            ssd1306_FillRect(2, 4, 124, 56, 0);
            ssd1306_DrawRect(0, 2, 128, 60, 1);
            ssd1306_DrawString(6, 14, "DOSE TAKEN", 2, 1, 0);
            ssd1306_DrawString(14, 40, "Recorded! Good job.", 1, 1, 0);
            break;
        }

        case SETUP_MENU: {
            ssd1306_DrawString(34, 2, "SETUP DOSE", 1, 1, 0);
            ssd1306_DrawLine(0, 12, 127, 12, 1);

            ssd1306_DrawString(10, 24, (setupSelectedPill == 1) ? "> PILL 1" : "  PILL 1", 1, 1, 0);
            ssd1306_DrawString(10, 40, (setupSelectedPill == 2) ? "> PILL 2" : "  PILL 2", 1, 1, 0);

            ssd1306_DrawLine(0, 52, 127, 52, 1);
            ssd1306_DrawString(2, 55, "B1:Move  B2:Select", 1, 1, 0);
            break;
        }

        case SETUP_EDIT_HOUR:
        case SETUP_EDIT_MINUTE: {
            ssd1306_DrawString(24, 2, (setupSelectedPill == 1) ? "PILL 1 TIME" : "PILL 2 TIME", 1, 1, 0);
            ssd1306_DrawLine(0, 12, 127, 12, 1);

            char buf[8];
            bool isEditHour = (currentState == SETUP_EDIT_HOUR);

            if (isEditHour && !cursorVisible) {
                ssd1306_DrawString(30, 24, "  ", 2, 1, 0);
            } else {
                snprintf(buf, sizeof(buf), "%02d", tempHour);
                ssd1306_DrawString(30, 24, buf, 2, 1, 0);
            }

            ssd1306_DrawString(56, 24, ":", 2, 1, 0);

            if (!isEditHour && !cursorVisible) {
                ssd1306_DrawString(70, 24, "  ", 2, 1, 0);
            } else {
                snprintf(buf, sizeof(buf), "%02d", tempMinute);
                ssd1306_DrawString(70, 24, buf, 2, 1, 0);
            }

            ssd1306_DrawString(isEditHour ? 35 : 75, 42, "^^", 1, 1, 0);
            ssd1306_DrawLine(0, 52, 127, 52, 1);
            ssd1306_DrawString(2, 55, isEditHour ? "B1:+Hour B2:Next" : "B1:+Min  B2:Save", 1, 1, 0);
            break;
        }

        case SETUP_CONFIRM: {
            ssd1306_DrawString(32, 2, "SAVE TIME?", 1, 1, 0);
            ssd1306_DrawLine(0, 12, 127, 12, 1);

            ssd1306_DrawString(30, 24, confirmSaveChoice ? "> YES, SAVE" : "  YES, SAVE", 1, 1, 0);
            ssd1306_DrawString(30, 38, !confirmSaveChoice ? "> CANCEL" : "  CANCEL", 1, 1, 0);

            ssd1306_DrawLine(0, 52, 127, 52, 1);
            ssd1306_DrawString(2, 55, "B1:Toggle  B2:Confirm", 1, 1, 0);
            break;
        }

        case SETUP_FEEDBACK: {
            ssd1306_DrawString(16, 12, (setupSelectedPill == 1) ? "PILL 1" : "PILL 2", 2, 1, 0);
            ssd1306_DrawString(24, 38, confirmSaveChoice ? "TIME SAVED!" : "CHANGES DISCARDED", 1, 1, 0);
            break;
        }
    }

    ssd1306_UpdateScreen();
}

/* ==============================================================================
 * HARDWARE ALARM INDICATORS (LEDS & BUZZER)
 * ============================================================================== */
static void updateAlarmIndicators(bool state, bool isPill1, bool isFastBlink) {
#if ENABLE_LEDS
    if (isPill1) {
        HAL_GPIO_WritePin(LED_PORT, LED_1_PIN, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(LED_PORT, LED_2_PIN, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
#else
    (void)isPill1;
#endif

#if ENABLE_BUZZER
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    (void)isFastBlink;
#endif
    (void)state;
}

static void stopAlarmIndicators(void) {
#if ENABLE_LEDS
    HAL_GPIO_WritePin(LED_PORT, LED_1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_PORT, LED_2_PIN, GPIO_PIN_RESET);
#endif

#if ENABLE_BUZZER
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
#endif
}

/* ==============================================================================
 * MAIN FUNCTION & APPLICATION LOOP
 * ============================================================================== */
int main(void) {
    // 1. STM32 Low-Level HAL & Clock Initialization
    HAL_Init();
    SystemClock_Config();

    // 2. Initialize GPIOs and I2C1 Peripheral
    MX_GPIO_Init();
    MX_I2C1_Init();

    // 3. Initialize SSD1306 OLED Display
    ssd1306_Init();

    // 4. Welcome Splash Screen
    ssd1306_Clear(0);
    ssd1306_DrawString(14, 14, "PILL BOX", 2, 1, 0);
    ssd1306_DrawString(20, 38, "INITIALIZING...", 1, 1, 0);
    ssd1306_UpdateScreen();
    HAL_Delay(1200);

    currentState = NORMAL_IDLE;
    updateScreen = true;
    lastTimeUpdate = HAL_GetTick();

    /* ==========================================================================
     * APPLICATION SUPER-LOOP
     * ========================================================================== */
    while (1) {
        uint32_t now = HAL_GetTick();

        // 1. Increment simulated 24-hour clock (every 1000 ms)
        if (now - lastTimeUpdate >= 1000) {
            lastTimeUpdate = now;
            simSecond++;
            if (simSecond >= 60) {
                simSecond = 0;
                simMinute++;
                if (simMinute >= 60) {
                    simMinute = 0;
                    simHour++;
                    if (simHour >= 24) {
                        simHour = 0;
                        simDay++;
                    }
                }
            }
            updateScreen = true;
        }

        // 2. Read Active LOW buttons with edge-detection debounce
        GPIO_PinState b1 = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_1_PIN);
        GPIO_PinState b2 = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_2_PIN);
        GPIO_PinState b3 = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_3_PIN);

        bool btn1Pressed = false;
        bool btn2Pressed = false;
        bool btn3Pressed = false;

        if (lastButton1State == GPIO_PIN_SET && b1 == GPIO_PIN_RESET) {
            if (now - lastButton1Press > DEBOUNCE_TIME) {
                lastButton1Press = now;
                btn1Pressed = true;
                updateScreen = true;
            }
        }

        if (lastButton2State == GPIO_PIN_SET && b2 == GPIO_PIN_RESET) {
            if (now - lastButton2Press > DEBOUNCE_TIME) {
                lastButton2Press = now;
                btn2Pressed = true;
                updateScreen = true;
            }
        }

        if (lastButton3State == GPIO_PIN_SET && b3 == GPIO_PIN_RESET) {
            if (now - lastButton3Press > DEBOUNCE_TIME) {
                lastButton3Press = now;
                btn3Pressed = true;
                updateScreen = true;
            }
        }

        lastButton1State = b1;
        lastButton2State = b2;
        lastButton3State = b3;

        // 3. Cursor blink timer during time editing
        if (currentState == SETUP_EDIT_HOUR || currentState == SETUP_EDIT_MINUTE) {
            if (now - lastBlinkToggle >= FIELD_BLINK_INTERVAL) {
                lastBlinkToggle = now;
                cursorVisible = !cursorVisible;
                updateScreen = true;
            }
        }

        // 4. System State Machine
        switch (currentState) {
            case NORMAL_IDLE:
                if (btn3Pressed) {
                    currentState = SETUP_MENU;
                    setupSelectedPill = 1;
                    updateScreen = true;
                }

                // Check Pill 1 Alarm Trigger
                if (simHour == dose1.hour && simMinute == dose1.minute && dose1.lastTriggeredDay != simDay) {
                    dose1.lastTriggeredDay = simDay;
                    alarmVisualState = false;
                    wrongButtonPressed = false;
                    currentBlinkInterval = NORMAL_BLINK_INTERVAL;
                    currentState = REMINDER_DOSE_1;
                    updateScreen = true;
                }

                // Check Pill 2 Alarm Trigger
                if (simHour == dose2.hour && simMinute == dose2.minute && dose2.lastTriggeredDay != simDay) {
                    dose2.lastTriggeredDay = simDay;
                    alarmVisualState = false;
                    wrongButtonPressed = false;
                    currentBlinkInterval = NORMAL_BLINK_INTERVAL;
                    currentState = REMINDER_DOSE_2;
                    updateScreen = true;
                }
                break;

            case REMINDER_DOSE_1:
                if (now - lastAlarmToggle >= currentBlinkInterval) {
                    lastAlarmToggle = now;
                    alarmVisualState = !alarmVisualState;
                    updateAlarmIndicators(alarmVisualState, true, wrongButtonPressed);
                    updateScreen = true;
                }

                // Button 1 is CORRECT for Pill 1
                if (btn1Pressed) {
                    stopAlarmIndicators();
                    doseConfirmedTimer = now;
                    currentState = DOSE_CONFIRMED;
                    updateScreen = true;
                }
                // Button 2 is WRONG for Pill 1
                else if (btn2Pressed && !wrongButtonPressed) {
                    wrongButtonPressed = true;
                    currentBlinkInterval = FAST_BLINK_INTERVAL;
                    updateScreen = true;
                }
                break;

            case REMINDER_DOSE_2:
                if (now - lastAlarmToggle >= currentBlinkInterval) {
                    lastAlarmToggle = now;
                    alarmVisualState = !alarmVisualState;
                    updateAlarmIndicators(alarmVisualState, false, wrongButtonPressed);
                    updateScreen = true;
                }

                // Button 2 is CORRECT for Pill 2
                if (btn2Pressed) {
                    stopAlarmIndicators();
                    doseConfirmedTimer = now;
                    currentState = DOSE_CONFIRMED;
                    updateScreen = true;
                }
                // Button 1 is WRONG for Pill 2
                else if (btn1Pressed && !wrongButtonPressed) {
                    wrongButtonPressed = true;
                    currentBlinkInterval = FAST_BLINK_INTERVAL;
                    updateScreen = true;
                }
                break;

            case DOSE_CONFIRMED:
                if (now - doseConfirmedTimer >= 2000) {
                    currentState = NORMAL_IDLE;
                    updateScreen = true;
                }
                break;

            case SETUP_MENU:
                if (btn1Pressed) {
                    setupSelectedPill = (setupSelectedPill == 1) ? 2 : 1;
                    updateScreen = true;
                }
                if (btn2Pressed) {
                    tempHour   = (setupSelectedPill == 1) ? dose1.hour : dose2.hour;
                    tempMinute = (setupSelectedPill == 1) ? dose1.minute : dose2.minute;
                    currentState = SETUP_EDIT_HOUR;
                    updateScreen = true;
                }
                break;

            case SETUP_EDIT_HOUR:
                if (btn1Pressed) {
                    tempHour = (tempHour + 1) % 24;
                    updateScreen = true;
                }
                if (btn2Pressed) {
                    currentState = SETUP_EDIT_MINUTE;
                    updateScreen = true;
                }
                break;

            case SETUP_EDIT_MINUTE:
                if (btn1Pressed) {
                    tempMinute = (tempMinute + 1) % 60;
                    updateScreen = true;
                }
                if (btn2Pressed) {
                    confirmSaveChoice = true;
                    currentState = SETUP_CONFIRM;
                    updateScreen = true;
                }
                break;

            case SETUP_CONFIRM:
                if (btn1Pressed) {
                    confirmSaveChoice = !confirmSaveChoice;
                    updateScreen = true;
                }
                if (btn2Pressed) {
                    if (confirmSaveChoice) {
                        if (setupSelectedPill == 1) {
                            dose1.hour = tempHour;
                            dose1.minute = tempMinute;
                            dose1.lastTriggeredDay = -1;
                        } else {
                            dose2.hour = tempHour;
                            dose2.minute = tempMinute;
                            dose2.lastTriggeredDay = -1;
                        }
                    }
                    feedbackTimer = now;
                    currentState = SETUP_FEEDBACK;
                    updateScreen = true;
                }
                break;

            case SETUP_FEEDBACK:
                if (now - feedbackTimer >= 1500) {
                    currentState = NORMAL_IDLE;
                    updateScreen = true;
                }
                break;
        }

        // 5. Redraw OLED screen when needed
        if (updateScreen) {
            renderScreen();
            updateScreen = false;
        }

        HAL_Delay(10);
    }
}

/* ==============================================================================
 * STM32 PERIPHERAL INITIALIZATION FUNCTIONS
 * ============================================================================== */

/**
 * @brief System Clock Configuration (72 MHz via HSE or 64 MHz HSI fallback)
 */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Configure HSE oscillator (8MHz external crystal on Blue Pill)
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9; // 8MHz * 9 = 72MHz
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        // Fallback to internal HSI if HSE crystal is not populated
        RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        RCC_OscInitStruct.HSEState = RCC_HSE_OFF;
        RCC_OscInitStruct.HSIState = RCC_HSI_ON;
        RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
        HAL_RCC_OscConfig(&RCC_OscInitStruct);
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
}

/**
 * @brief I2C1 Initialization (PB6 = SCL, PB7 = SDA, 100 kHz)
 */
static void MX_I2C1_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
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

/**
 * @brief GPIO Initialization (PB12, PB14, PB15 as Pull-up inputs)
 */
static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Enable GPIOB and GPIOA clocks
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // Configure Buttons: PB12, PB14, PB15 with internal pull-up
    GPIO_InitStruct.Pin = BUTTON_1_PIN | BUTTON_2_PIN | BUTTON_3_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BUTTON_PORT, &GPIO_InitStruct);

    // Configure I2C1 pins: PB6 (SCL), PB7 (SDA) as Alternate Function Open Drain
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

#if ENABLE_LEDS
    GPIO_InitStruct.Pin = LED_1_PIN | LED_2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LED_PORT, LED_1_PIN | LED_2_PIN, GPIO_PIN_RESET);
#endif

#if ENABLE_BUZZER
    GPIO_InitStruct.Pin = BUZZER_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
#endif
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}
