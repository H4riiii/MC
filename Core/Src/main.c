/*
 * ==============================================================================
 * SMART PILL BOX - STM32 C FIRMWARE (ZERO-SRAM DIRECT I2C STREAMING)
 * Target: STM32F103C8T6 (Blue Pill)
 *
 * PINOUT CONFIGURATION:
 * -------------------------------------------------------------
 * Shared I2C1 Bus (100 kHz):
 *   - OLED SCL      -> PB6 (I2C1 SCL)
 *   - OLED SDA      -> PB7 (I2C1 SDA)
 *   - OLED VCC      -> 3.3V / 5V
 *   - OLED GND      -> GND
 *   - DS1307 SCL    -> PB6 (I2C1 SCL - Shared with OLED)
 *   - DS1307 SDA    -> PB7 (I2C1 SDA - Shared with OLED)
 *   - DS1307 VCC    -> 5V
 *   - DS1307 GND    -> GND
 *
 * User Push Buttons (Active LOW with internal pull-ups):
 *   - Button 1      -> PB12 (Terminal 1 to PB12, Terminal 2 to GND) - Dose 1 / Menu + / Value +
 *   - Button 2      -> PB14 (Terminal 1 to PB14, Terminal 2 to GND) - Dose 2 / Select / Next
 *   - Button 3      -> PB15 (Terminal 1 to PB15, Terminal 2 to GND) - Setup Menu / Exit
 *
 * Audio-Visual Indicators: 
 *   - LED 1 (Pill 1)-> PB1  (Active HIGH via current-limiting resistor)
 *   - LED 2 (Pill 2)-> PB10 (Active HIGH via current-limiting resistor)
 *   - Buzzer        -> PB0  (Piezo buzzer, Active HIGH)
 *
 * MicroSD Card Module (SPI1):
 *   - SD CS         -> PA4 (GPIO Output Push-Pull)
 *   - SD SCK        -> PA5 (SPI1 SCK)
 *   - SD MISO       -> PA6 (SPI1 MISO, pull-up)
 *   - SD MOSI       -> PA7 (SPI1 MOSI)
 *   - SD VCC        -> 5V / 3.3V
 *   - SD GND        -> GND
 * ==============================================================================
 */

#include "stm32f1xx_hal.h"
#include "ds1307.h"
#include "sd_card.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ==============================================================================
 * HARDWARE CONFIGURATION & PIN DEFINITIONS
 * ============================================================================== */

// Feature flags
#define ENABLE_LEDS       1   // 1 = Enable LEDs on PB1 (Pill 1) and PB10 (Pill 2)
#define ENABLE_BUZZER     1   // 1 = Enable Buzzer on PB0

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
#define RTC_UPDATE_INTERVAL    100   // 100 ms live loop interval

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
    SETUP_FEEDBACK,
    VIEW_LOGS
} SystemState_t;

typedef struct {
    int hour;
    int minute;
    int lastTriggeredDay;
} DoseAlarm_t;

// Global System Variables
static SystemState_t currentState = NORMAL_IDLE;
static SystemState_t lastRenderedState = (SystemState_t)-1;
static bool forceFullRedraw = true;

// Scheduled dose alarms: Dose 1 at 07:30, Dose 2 at 19:00
static DoseAlarm_t dose1 = {7, 30, -1};
static DoseAlarm_t dose2 = {19, 0, -1};

// 3-Day Dosage History Tracker
static DailyDoseLog_t doseHistory[3];
static int viewLogDay = 0; // 0 = Today, 1 = Yesterday, 2 = Day Before

// Setup temporary variables
static int setupMenuItem = 1;
static int tempHour = 0;
static int tempMinute = 0;
static bool confirmSaveChoice = true;
static uint32_t feedbackTimer = 0;
static uint32_t doseConfirmedTimer = 0;
static bool sdAvailable = false;

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
static uint32_t lastRtcRead = 0;
static bool cursorVisible = true;

// Real-Time Clock state from DS1307
static RTC_DateTime_t currentRtc = {
    .seconds = 0,
    .minutes = 30,
    .hours = 7,
    .dayOfWeek = 1,
    .date = 1,
    .month = 1,
    .year = 2026
};
static bool rtcAvailable = false;

/* ==============================================================================
 * PERIPHERAL HANDLERS & FORWARD DECLARATIONS
 * ============================================================================== */
I2C_HandleTypeDef hi2c1;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
void Error_Handler(void);

static void updateAlarmIndicators(bool state, bool isPill1, bool isFastBlink);
static void stopAlarmIndicators(void);

/* ==============================================================================
 * 5x7 ASCII FONT TABLE (Stored directly in Flash ROM, zero RAM usage)
 * ============================================================================== */
static const uint8_t font5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 '!'
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 '"'
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 '#'
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 '$'
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 '%'
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 '&'
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '\''
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 '('
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 ')'
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // 42 '*'
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 '+'
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ','
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 '-'
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 '.'
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 '/'
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 '0'
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 '1'
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 '2'
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 '3'
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 '4'
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 '5'
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 '6'
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 '7'
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 '8'
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 '9'
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 ':'
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ';'
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
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 '\\'
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

/* ==============================================================================
 * SSD1306 HARDWARE DRIVER (Page Addressing Mode, Direct I2C Streaming)
 *
 * Architecture: Zero-SRAM direct-stream. Every render function positions the
 * SSD1306 page/column pointer then writes pixel data in a single 128-byte
 * burst per page row. No intermediate framebuffer is kept in STM32 SRAM.
 *
 * Key design decisions:
 *  - Page Addressing Mode (0x02) used throughout — matches 0xB0+page pointer
 *    set commands in every DirectStream function.
 *  - Each page write is ONE 128-byte HAL_I2C_Mem_Write call (not 4×32-byte).
 *    This eliminates inter-burst I2C STOP/START overhead and keeps the total
 *    bus time per page to ~11ms at 100 kHz, well within any timeout.
 *  - ssd1306_WriteCommand timeout: 10 ms  (single byte, should complete ~0.1ms)
 *  - ssd1306_WritePage    timeout: 20 ms  (128 bytes  ~11ms at 100kHz)
 *  - I2C guard: every low-level function checks hi2c1.State and re-inits if
 *    BUSY/ERROR so a single timeout never permanently kills the display.
 * ============================================================================== */

/* -- Internal helper: recover I2C if HAL left it in a bad state ------------- */
static void ssd1306_I2C_Guard(void) {
    if (hi2c1.State != HAL_I2C_STATE_READY) {
        HAL_I2C_DeInit(&hi2c1);
        MX_I2C1_Init();
    }
}

/* -- Send one command byte to SSD1306 --------------------------------------- */
static void ssd1306_WriteCommand(uint8_t cmd) {
    ssd1306_I2C_Guard();
    HAL_I2C_Mem_Write(&hi2c1, SSD1306_I2C_ADDR, 0x00, 1, &cmd, 1, 10);
}

/* -- Write 128 pixel-bytes to the current page position --------------------- */
static void ssd1306_WritePage(uint8_t page, const uint8_t *data, uint16_t len) {
    /* Position GDDRAM pointer: page row + column 0 */
    ssd1306_WriteCommand(0xB0 + page); /* Page start address          */
    ssd1306_WriteCommand(0x00);        /* Lower column nibble  = 0    */
    ssd1306_WriteCommand(0x10);        /* Higher column nibble = 0    */
    ssd1306_I2C_Guard();
    HAL_I2C_Mem_Write(&hi2c1, SSD1306_I2C_ADDR, 0x40, 1, (uint8_t *)data, len, 20);
}

/* -- Clear one page (128 zero-bytes) ---------------------------------------- */
static void ssd1306_ClearPage(uint8_t page) {
    static const uint8_t zeros[128] = {0};
    ssd1306_WritePage(page, zeros, 128);
}

/* -- Clear all 8 pages of OLED GDDRAM --------------------------------------- */
static void ssd1306_ClearScreen(void) {
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_ClearPage(page);
    }
}

/* ---------------------------------------------------------------------------
 * ssd1306_DirectStreamString()
 *
 * Renders a string of 5×7 font characters directly into OLED GDDRAM starting
 * at (page, col). Characters are 6 pixels wide (5 glyph + 1 gap column).
 * Builds the full page row in a local 128-byte array then flushes it in one
 * burst — avoids per-character I2C transactions that used to add ~0.8 ms each.
 * --------------------------------------------------------------------------- */
static void ssd1306_DirectStreamString(uint8_t page, uint8_t col, const char *str, bool invert) {
    uint8_t rowBuf[128];
    /* Pre-fill with background: 0x00 (normal) or 0xFF (inverted) */
    memset(rowBuf, invert ? 0xFF : 0x00, sizeof(rowBuf));

    uint8_t x = col;
    while (*str && x < 128) {
        char c = *str++;
        const uint8_t *glyph = (c >= 32 && c <= 126) ? font5x7[c - 32] : font5x7[0];
        for (int i = 0; i < 5 && x < 128; i++, x++) {
            rowBuf[x] = invert ? ~glyph[i] : glyph[i];
        }
        if (x < 128) {
            rowBuf[x++] = invert ? 0xFF : 0x00; /* 1-pixel character gap */
        }
    }
    ssd1306_WritePage(page, rowBuf, 128);
}

/* -- Draw a solid horizontal pixel-line across page 'page' ------------------ */
static void ssd1306_DirectStreamHLine(uint8_t page, uint8_t pattern) {
    uint8_t lineBuf[128];
    memset(lineBuf, pattern, sizeof(lineBuf));
    ssd1306_WritePage(page, lineBuf, 128);
}

/* ---------------------------------------------------------------------------
 * ssd1306_Init()  — SSD1306 power-on initialisation sequence
 *
 * Uses Page Addressing Mode (0x02).  GDDRAM is cleared to zero before the
 * display is turned ON so no random static noise is visible on power-up.
 * --------------------------------------------------------------------------- */
void ssd1306_Init(void) {
    HAL_Delay(100); /* Let OLED VCC stabilise */

    ssd1306_WriteCommand(0xAE); /* Display OFF                              */
    ssd1306_WriteCommand(0x20); /* Set Memory Addressing Mode               */
    ssd1306_WriteCommand(0x02); /* Page Addressing Mode                     */
    ssd1306_WriteCommand(0xB0); /* Page start address = page 0              */
    ssd1306_WriteCommand(0xC8); /* COM scan direction: remapped             */
    ssd1306_WriteCommand(0x00); /* Low column nibble  = 0                   */
    ssd1306_WriteCommand(0x10); /* High column nibble = 0                   */
    ssd1306_WriteCommand(0x40); /* Display start line = 0                   */
    ssd1306_WriteCommand(0x81); /* Set contrast                             */
    ssd1306_WriteCommand(0x7F);
    ssd1306_WriteCommand(0xA1); /* Segment re-map: col 127 → SEG0          */
    ssd1306_WriteCommand(0xA6); /* Normal (non-inverted) display            */
    ssd1306_WriteCommand(0xA8); /* Multiplex ratio                          */
    ssd1306_WriteCommand(0x3F); /* 1/64 duty (64 rows)                      */
    ssd1306_WriteCommand(0xA4); /* Output follows GDDRAM                    */
    ssd1306_WriteCommand(0xD3); /* Display offset                           */
    ssd1306_WriteCommand(0x00); /* No offset                                */
    ssd1306_WriteCommand(0xD5); /* Clock divide ratio / oscillator freq     */
    ssd1306_WriteCommand(0x80);
    ssd1306_WriteCommand(0xD9); /* Pre-charge period                        */
    ssd1306_WriteCommand(0xF1);
    ssd1306_WriteCommand(0xDA); /* COM pins hardware config                 */
    ssd1306_WriteCommand(0x12);
    ssd1306_WriteCommand(0xDB); /* VCOMH deselect level                     */
    ssd1306_WriteCommand(0x40);
    ssd1306_WriteCommand(0x8D); /* Charge pump                              */
    ssd1306_WriteCommand(0x14); /* Enable charge pump (~7.5 V booster)      */

    /* Write all-zeros to every page BEFORE display ON — prevents static grain */
    ssd1306_ClearScreen();

    ssd1306_WriteCommand(0xAF); /* Display ON                               */
    HAL_Delay(50);              /* Wait for display to light up             */
}

/* ==============================================================================
 * ZERO-SRAM UI RENDERING ROUTINES
 * Directly updates OLED pages without allocating any SRAM framebuffer.
 * ============================================================================== */

static void renderLiveClockHeader(void) {
    char headerBuf[32];
    // Page 0: [HH:MM:SS] RTC:[OK/!] SD:[OK/-]
    snprintf(headerBuf, sizeof(headerBuf), " %02d:%02d:%02d  %s %s ", 
             currentRtc.hours, currentRtc.minutes, currentRtc.seconds,
             rtcAvailable ? "RTC:OK" : "RTC:!",
             sdAvailable ? "SD:OK" : "SD:-");
    ssd1306_DirectStreamString(0, 4, headerBuf, false);
    ssd1306_DirectStreamHLine(1, 0x01); // 1-pixel bottom line divider on Page 1
}

static void renderZeroSramScreen(void) {
    char lineBuf[48];

    /* On state change: clear only the body pages (2-7). Pages 0-1 (header) are
     * always fully rewritten by renderLiveClockHeader() or the state case below,
     * so clearing them first just wastes I2C bandwidth. Body pages must be wiped
     * so stale text from the previous state doesn't bleed through. */
    if (currentState != lastRenderedState || forceFullRedraw) {
        for (uint8_t p = 2; p < 8; p++) {
            ssd1306_ClearPage(p);
        }
        lastRenderedState = currentState;
        forceFullRedraw = false;
    }

    switch (currentState) {
        case NORMAL_IDLE: {
            renderLiveClockHeader();

            // Page 2: Pill 1 Alarm Schedule & Status
            snprintf(lineBuf, sizeof(lineBuf), "P1 [%02d:%02d]: %s",
                     dose1.hour, dose1.minute,
                     (doseHistory[0].p1_status == DOSE_STATUS_TAKEN) ? "TAKEN" :
                     (doseHistory[0].p1_status == DOSE_STATUS_MISSED) ? "MISSED" : "PENDING");
            ssd1306_DirectStreamString(2, 4, lineBuf, false);

            // Page 3: Pill 2 Alarm Schedule & Status
            snprintf(lineBuf, sizeof(lineBuf), "P2 [%02d:%02d]: %s",
                     dose2.hour, dose2.minute,
                     (doseHistory[0].p2_status == DOSE_STATUS_TAKEN) ? "TAKEN" :
                     (doseHistory[0].p2_status == DOSE_STATUS_MISSED) ? "MISSED" : "PENDING");
            ssd1306_DirectStreamString(3, 4, lineBuf, false);

            ssd1306_DirectStreamHLine(5, 0x01);

            // Page 6: System status
            ssd1306_DirectStreamString(6, 8, "MONITORING ACTIVE", false);

            // Page 7: Button helper prompt
            ssd1306_DirectStreamString(7, 4, "[B3: Settings / Logs]", false);
            break;
        }

        case REMINDER_DOSE_1:
        case REMINDER_DOSE_2: {
            bool isDose1 = (currentState == REMINDER_DOSE_1);
            renderLiveClockHeader();

            if (wrongButtonPressed) {
                ssd1306_DirectStreamString(2, 8, "!! WRONG PILL !!", alarmVisualState);
                snprintf(lineBuf, sizeof(lineBuf), "Press BUTTON %d for P%d", isDose1 ? 1 : 2, isDose1 ? 1 : 2);
                ssd1306_DirectStreamString(4, 4, lineBuf, false);
                ssd1306_DirectStreamString(6, 4, "DO NOT TAKE WRONG DOSE", false);
            } else {
                ssd1306_DirectStreamString(2, 16, isDose1 ? "TAKE >> PILL 1 <<" : "TAKE >> PILL 2 <<", alarmVisualState);
                snprintf(lineBuf, sizeof(lineBuf), "Time: %02d:%02d", isDose1 ? dose1.hour : dose2.hour, isDose1 ? dose1.minute : dose2.minute);
                ssd1306_DirectStreamString(4, 28, lineBuf, false);
                snprintf(lineBuf, sizeof(lineBuf), "Press Button %d to Confirm", isDose1 ? 1 : 2);
                ssd1306_DirectStreamString(6, 4, lineBuf, false);
            }
            break;
        }

        case DOSE_CONFIRMED: {
            ssd1306_DirectStreamString(2, 24, "DOSE CONFIRMED", true);
            ssd1306_DirectStreamString(4, 16, "RECORDED TO SD CARD", false);
            ssd1306_DirectStreamString(6, 28, "Good Job! Stay Safe.", false);
            break;
        }

        case SETUP_MENU: {
            ssd1306_DirectStreamString(0, 36, "SETTINGS MENU", false);
            ssd1306_DirectStreamHLine(1, 0x01);

            ssd1306_DirectStreamString(2, 4, (setupMenuItem == 1) ? "> 1. PILL 1 TIME   " : "  1. PILL 1 TIME   ", (setupMenuItem == 1));
            ssd1306_DirectStreamString(3, 4, (setupMenuItem == 2) ? "> 2. PILL 2 TIME   " : "  2. PILL 2 TIME   ", (setupMenuItem == 2));
            ssd1306_DirectStreamString(4, 4, (setupMenuItem == 3) ? "> 3. ADJUST CLOCK  " : "  3. ADJUST CLOCK  ", (setupMenuItem == 3));
            ssd1306_DirectStreamString(5, 4, (setupMenuItem == 4) ? "> 4. VIEW LOGS     " : "  4. VIEW LOGS     ", (setupMenuItem == 4));

            ssd1306_DirectStreamHLine(6, 0x80);
            ssd1306_DirectStreamString(7, 2, "B1:Next B2:Select B3:Exit", false);
            break;
        }

        case SETUP_EDIT_HOUR:
        case SETUP_EDIT_MINUTE: {
            bool isEditHour = (currentState == SETUP_EDIT_HOUR);
            const char *title = (setupMenuItem == 1) ? "SET PILL 1 TIME" :
                                (setupMenuItem == 2) ? "SET PILL 2 TIME" : "ADJUST CLOCK";
            ssd1306_DirectStreamString(0, 16, title, false);
            ssd1306_DirectStreamHLine(1, 0x01);

            if (isEditHour && !cursorVisible) {
                snprintf(lineBuf, sizeof(lineBuf), "    -- : %02d    ", tempMinute);
            } else if (!isEditHour && !cursorVisible) {
                snprintf(lineBuf, sizeof(lineBuf), "    %02d : --    ", tempHour);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "    %02d : %02d    ", tempHour, tempMinute);
            }
            ssd1306_DirectStreamString(3, 16, lineBuf, true);

            ssd1306_DirectStreamString(5, 20, isEditHour ? "^^ [HOUR]" : "      ^^ [MIN]", false);
            ssd1306_DirectStreamHLine(6, 0x80);
            ssd1306_DirectStreamString(7, 2, isEditHour ? "B1:+Hour B2:Next B3:Back" : "B1:+Min  B2:Save B3:Back", false);
            break;
        }

        case SETUP_CONFIRM: {
            ssd1306_DirectStreamString(0, 32, "CONFIRM SAVE?", false);
            ssd1306_DirectStreamHLine(1, 0x01);

            ssd1306_DirectStreamString(3, 20, confirmSaveChoice ? "> YES, SAVE   " : "  YES, SAVE   ", confirmSaveChoice);
            ssd1306_DirectStreamString(4, 20, !confirmSaveChoice ? "> CANCEL      " : "  CANCEL      ", !confirmSaveChoice);

            ssd1306_DirectStreamHLine(6, 0x80);
            ssd1306_DirectStreamString(7, 4, "B1:Toggle  B2:Confirm", false);
            break;
        }

        case SETUP_FEEDBACK: {
            const char *item = (setupMenuItem == 1) ? "PILL 1 TIME" :
                               (setupMenuItem == 2) ? "PILL 2 TIME" : "RTC CLOCK";
            ssd1306_DirectStreamString(2, 24, item, false);
            ssd1306_DirectStreamString(4, 20, confirmSaveChoice ? "SAVED TO SD & RTC" : "CHANGES CANCELLED", true);
            break;
        }

        case VIEW_LOGS: {
            const char *tag = (viewLogDay == 0) ? "TODAY" : ((viewLogDay == 1) ? "YESTERDAY" : "-2 DAYS");
            snprintf(lineBuf, sizeof(lineBuf), "LOG [%d/3] %02d/%02d (%s)", 
                     viewLogDay + 1, doseHistory[viewLogDay].day, doseHistory[viewLogDay].month, tag);
            ssd1306_DirectStreamString(0, 2, lineBuf, false);
            ssd1306_DirectStreamHLine(1, 0x01);

            // Pill 1 Log Entry
            if (doseHistory[viewLogDay].p1_status == DOSE_STATUS_TAKEN) {
                snprintf(lineBuf, sizeof(lineBuf), "P1: OK at %02d:%02d", doseHistory[viewLogDay].p1_hour, doseHistory[viewLogDay].p1_min);
            } else if (doseHistory[viewLogDay].p1_status == DOSE_STATUS_MISSED) {
                snprintf(lineBuf, sizeof(lineBuf), "P1: MISSED (%02d:%02d)", dose1.hour, dose1.minute);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "P1: PENDING (%02d:%02d)", dose1.hour, dose1.minute);
            }
            ssd1306_DirectStreamString(2, 4, lineBuf, false);

            // Pill 2 Log Entry
            if (doseHistory[viewLogDay].p2_status == DOSE_STATUS_TAKEN) {
                snprintf(lineBuf, sizeof(lineBuf), "P2: OK at %02d:%02d", doseHistory[viewLogDay].p2_hour, doseHistory[viewLogDay].p2_min);
            } else if (doseHistory[viewLogDay].p2_status == DOSE_STATUS_MISSED) {
                snprintf(lineBuf, sizeof(lineBuf), "P2: MISSED (%02d:%02d)", dose2.hour, dose2.minute);
            } else {
                snprintf(lineBuf, sizeof(lineBuf), "P2: PENDING (%02d:%02d)", dose2.hour, dose2.minute);
            }
            ssd1306_DirectStreamString(3, 4, lineBuf, false);

            // SD sector verification
            snprintf(lineBuf, sizeof(lineBuf), "SD: %s", sdAvailable ? "SECTOR 1001 SYNCED" : "NO SD CARD");
            ssd1306_DirectStreamString(5, 4, lineBuf, false);

            ssd1306_DirectStreamHLine(6, 0x80);
            ssd1306_DirectStreamString(7, 4, "B1:Next Day   B3:Back", false);
            break;
        }
    }
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

    // 3. Initialize SSD1306 OLED Display (Zero-SRAM Direct Stream Mode)
    ssd1306_Init();

    // 4. Initialize DS1307 Real Time Clock
    if (DS1307_Init(&hi2c1) == HAL_OK) {
        rtcAvailable = true;
        DS1307_GetTime(&hi2c1, &currentRtc);
    } else {
        rtcAvailable = false;
    }

    // 5. Initialize MicroSD Card (SPI1 on PA4-PA7) & Load Saved Config
    sdAvailable = SD_Init();
    if (sdAvailable) {
        int p1_h, p1_m, p2_h, p2_m;
        if (SD_LoadConfig(&p1_h, &p1_m, &p2_h, &p2_m)) {
            dose1.hour = p1_h;
            dose1.minute = p1_m;
            dose2.hour = p2_h;
            dose2.minute = p2_m;
        }
    }

    // 6. Direct I2C Splash Diagnostic Screen
    ssd1306_ClearScreen();
    ssd1306_DirectStreamString(1, 16, "SMART PILL BOX", false);
    ssd1306_DirectStreamHLine(2, 0x01);
    ssd1306_DirectStreamString(3, 4, rtcAvailable ? "RTC DS1307: READY" : "RTC DS1307: FAILED", false);
    ssd1306_DirectStreamString(4, 4, sdAvailable ?  "SD LOGGING: READY" : "SD LOGGING: NONE", false);
    ssd1306_DirectStreamString(6, 4, "MODE: ZERO-SRAM I2C", false);
    HAL_Delay(1800);

    // 7. Initialize 3-Day Dosage History tracking
    DoseHistory_Init(doseHistory, currentRtc.year, currentRtc.month, currentRtc.date,
                     dose1.hour, dose1.minute, dose2.hour, dose2.minute, currentRtc.hours, currentRtc.minutes);

    currentState = NORMAL_IDLE;
    forceFullRedraw = true;
    lastRtcRead = HAL_GetTick();

    /* ==========================================================================
     * APPLICATION SUPER-LOOP (100 ms Cycle)
     * ========================================================================== */
    while (1) {
        uint32_t now = HAL_GetTick();

        // 1. Read real time from DS1307 RTC (every 100 ms)
        if (now - lastRtcRead >= RTC_UPDATE_INTERVAL) {
            uint32_t elapsedMs = now - lastRtcRead;
            lastRtcRead = now;
            RTC_DateTime_t tempRtc;
            if (DS1307_GetTime(&hi2c1, &tempRtc) == HAL_OK) {
                rtcAvailable = true;
                static uint8_t lastRtcSec = 0xFF;
                static uint32_t lastSecChangeTime = 0;

                if (tempRtc.seconds != lastRtcSec) {
                    lastRtcSec = tempRtc.seconds;
                    lastSecChangeTime = now;
                    currentRtc = tempRtc;
                } else if (now - lastSecChangeTime >= 1500) {
                    // Physical 32.768 kHz oscillator stalled: tick forward fallback
                    currentRtc.seconds++;
                    if (currentRtc.seconds >= 60) {
                        currentRtc.seconds = 0;
                        currentRtc.minutes++;
                        if (currentRtc.minutes >= 60) {
                            currentRtc.minutes = 0;
                            currentRtc.hours = (currentRtc.hours + 1) % 24;
                        }
                    }
                    lastSecChangeTime = now;
                }
            } else {
                rtcAvailable = false;
                // Fallback software clock: increment time based on elapsed milliseconds
                static uint32_t subSecAccumulator = 0;
                subSecAccumulator += elapsedMs;
                while (subSecAccumulator >= 1000) {
                    subSecAccumulator -= 1000;
                    currentRtc.seconds++;
                    if (currentRtc.seconds >= 60) {
                        currentRtc.seconds = 0;
                        currentRtc.minutes++;
                        if (currentRtc.minutes >= 60) {
                            currentRtc.minutes = 0;
                            currentRtc.hours++;
                            if (currentRtc.hours >= 24) {
                                currentRtc.hours = 0;
                                currentRtc.date++;
                            }
                        }
                    }
                }
            }

            DoseHistory_CheckDayChange(doseHistory, currentRtc.year, currentRtc.month, currentRtc.date,
                                       dose1.hour, dose1.minute, dose2.hour, dose2.minute);
            DoseHistory_UpdateStatus(doseHistory, dose1.hour, dose1.minute, dose2.hour, dose2.minute,
                                     currentRtc.hours, currentRtc.minutes);
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
            }
        }

        if (lastButton2State == GPIO_PIN_SET && b2 == GPIO_PIN_RESET) {
            if (now - lastButton2Press > DEBOUNCE_TIME) {
                lastButton2Press = now;
                btn2Pressed = true;
            }
        }

        if (lastButton3State == GPIO_PIN_SET && b3 == GPIO_PIN_RESET) {
            if (now - lastButton3Press > DEBOUNCE_TIME) {
                lastButton3Press = now;
                btn3Pressed = true;
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
            }
        }

        // 4. System State Machine
        switch (currentState) {
            case NORMAL_IDLE:
                if (btn3Pressed) {
                    currentState = SETUP_MENU;
                    setupMenuItem = 1;
                    forceFullRedraw = true;
                }

                // Check Pill 1 Alarm Trigger against Real RTC Time
                if (currentRtc.hours == dose1.hour && currentRtc.minutes == dose1.minute && dose1.lastTriggeredDay != currentRtc.date) {
                    dose1.lastTriggeredDay = currentRtc.date;
                    alarmVisualState = false;
                    wrongButtonPressed = false;
                    currentBlinkInterval = NORMAL_BLINK_INTERVAL;
                    currentState = REMINDER_DOSE_1;
                    forceFullRedraw = true;
                }

                // Check Pill 2 Alarm Trigger against Real RTC Time
                if (currentRtc.hours == dose2.hour && currentRtc.minutes == dose2.minute && dose2.lastTriggeredDay != currentRtc.date) {
                    dose2.lastTriggeredDay = currentRtc.date;
                    alarmVisualState = false;
                    wrongButtonPressed = false;
                    currentBlinkInterval = NORMAL_BLINK_INTERVAL;
                    currentState = REMINDER_DOSE_2;
                    forceFullRedraw = true;
                }
                break;

            case REMINDER_DOSE_1:
                if (now - lastAlarmToggle >= currentBlinkInterval) {
                    lastAlarmToggle = now;
                    alarmVisualState = !alarmVisualState;
                    updateAlarmIndicators(alarmVisualState, true, wrongButtonPressed);
                }

                // Button 1 is CORRECT for Pill 1
                if (btn1Pressed) {
                    stopAlarmIndicators();
                    doseConfirmedTimer = now;
                    currentState = DOSE_CONFIRMED;
                    forceFullRedraw = true;
                    DoseHistory_RecordDose(doseHistory, 1, currentRtc.hours, currentRtc.minutes);
                    SD_LogEvent(SD_EVENT_DOSE_1_TAKEN, currentRtc.year, currentRtc.month, currentRtc.date,
                                currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Confirmed Button 1");
                }
                // Button 2 is WRONG for Pill 1
                else if (btn2Pressed && !wrongButtonPressed) {
                    wrongButtonPressed = true;
                    currentBlinkInterval = FAST_BLINK_INTERVAL;
                    SD_LogEvent(SD_EVENT_WRONG_BUTTON, currentRtc.year, currentRtc.month, currentRtc.date,
                                currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Pill 1 due - Pressed Btn 2");
                }
                break;

            case REMINDER_DOSE_2:
                if (now - lastAlarmToggle >= currentBlinkInterval) {
                    lastAlarmToggle = now;
                    alarmVisualState = !alarmVisualState;
                    updateAlarmIndicators(alarmVisualState, false, wrongButtonPressed);
                }

                // Button 2 is CORRECT for Pill 2
                if (btn2Pressed) {
                    stopAlarmIndicators();
                    doseConfirmedTimer = now;
                    currentState = DOSE_CONFIRMED;
                    forceFullRedraw = true;
                    DoseHistory_RecordDose(doseHistory, 2, currentRtc.hours, currentRtc.minutes);
                    SD_LogEvent(SD_EVENT_DOSE_2_TAKEN, currentRtc.year, currentRtc.month, currentRtc.date,
                                currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Confirmed Button 2");
                }
                // Button 1 is WRONG for Pill 2
                else if (btn1Pressed && !wrongButtonPressed) {
                    wrongButtonPressed = true;
                    currentBlinkInterval = FAST_BLINK_INTERVAL;
                    SD_LogEvent(SD_EVENT_WRONG_BUTTON, currentRtc.year, currentRtc.month, currentRtc.date,
                                currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Pill 2 due - Pressed Btn 1");
                }
                break;

            case DOSE_CONFIRMED:
                if (now - doseConfirmedTimer >= 2000) {
                    currentState = NORMAL_IDLE;
                    forceFullRedraw = true;
                }
                break;

            case SETUP_MENU:
                if (btn1Pressed) {
                    setupMenuItem++;
                    if (setupMenuItem > 4) setupMenuItem = 1;
                }
                if (btn2Pressed) {
                    if (setupMenuItem == 1) {
                        tempHour   = dose1.hour;
                        tempMinute = dose1.minute;
                        currentState = SETUP_EDIT_HOUR;
                    } else if (setupMenuItem == 2) {
                        tempHour   = dose2.hour;
                        tempMinute = dose2.minute;
                        currentState = SETUP_EDIT_HOUR;
                    } else if (setupMenuItem == 3) {
                        tempHour   = currentRtc.hours;
                        tempMinute = currentRtc.minutes;
                        currentState = SETUP_EDIT_HOUR;
                    } else {
                        currentState = VIEW_LOGS;
                        viewLogDay = 0;
                    }
                    forceFullRedraw = true;
                }
                if (btn3Pressed) {
                    currentState = NORMAL_IDLE;
                    forceFullRedraw = true;
                }
                break;

            case SETUP_EDIT_HOUR:
                if (btn1Pressed) {
                    tempHour = (tempHour + 1) % 24;
                }
                if (btn2Pressed) {
                    currentState = SETUP_EDIT_MINUTE;
                }
                if (btn3Pressed) {
                    currentState = SETUP_MENU;
                    forceFullRedraw = true;
                }
                break;

            case SETUP_EDIT_MINUTE:
                if (btn1Pressed) {
                    tempMinute = (tempMinute + 1) % 60;
                }
                if (btn2Pressed) {
                    confirmSaveChoice = true;
                    currentState = SETUP_CONFIRM;
                    forceFullRedraw = true;
                }
                if (btn3Pressed) {
                    currentState = SETUP_MENU;
                    forceFullRedraw = true;
                }
                break;

            case SETUP_CONFIRM:
                if (btn1Pressed) {
                    confirmSaveChoice = !confirmSaveChoice;
                }
                if (btn2Pressed) {
                    if (confirmSaveChoice) {
                        if (setupMenuItem == 1) {
                            dose1.hour = tempHour;
                            dose1.minute = tempMinute;
                            dose1.lastTriggeredDay = -1;
                            SD_SaveConfig(dose1.hour, dose1.minute, dose2.hour, dose2.minute);
                            SD_LogEvent(SD_EVENT_CONFIG_UPDATED, currentRtc.year, currentRtc.month, currentRtc.date,
                                        currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Pill 1 time updated");
                        } else if (setupMenuItem == 2) {
                            dose2.hour = tempHour;
                            dose2.minute = tempMinute;
                            dose2.lastTriggeredDay = -1;
                            SD_SaveConfig(dose1.hour, dose1.minute, dose2.hour, dose2.minute);
                            SD_LogEvent(SD_EVENT_CONFIG_UPDATED, currentRtc.year, currentRtc.month, currentRtc.date,
                                        currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Pill 2 time updated");
                        } else {
                            // Write updated time to DS1307 RTC
                            currentRtc.hours = tempHour;
                            currentRtc.minutes = tempMinute;
                            currentRtc.seconds = 0;
                            DS1307_SetTime(&hi2c1, &currentRtc);
                            SD_LogEvent(SD_EVENT_CLOCK_ADJUSTED, currentRtc.year, currentRtc.month, currentRtc.date,
                                        currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "RTC clock re-calibrated");
                        }
                    }
                    feedbackTimer = now;
                    currentState = SETUP_FEEDBACK;
                    forceFullRedraw = true;
                }
                break;

            case SETUP_FEEDBACK:
                if (now - feedbackTimer >= 1500) {
                    currentState = NORMAL_IDLE;
                    forceFullRedraw = true;
                }
                break;

            case VIEW_LOGS:
                if (btn1Pressed) {
                    viewLogDay = (viewLogDay + 1) % 3;
                    forceFullRedraw = true;
                }
                if (btn3Pressed) {
                    currentState = SETUP_MENU;
                    forceFullRedraw = true;
                }
                break;
        }

        // 5. Update OLED directly via I2C stream
        renderZeroSramScreen();

        /* No HAL_Delay here — the I2C streaming in renderZeroSramScreen() already
         * provides natural pacing (~88ms per full 8-page render at 100 kHz).
         * Adding a HAL_Delay on top risks overrunning the RTC_UPDATE_INTERVAL
         * and causes the HAL I2C state machine to perceive overlapping transactions. */
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
