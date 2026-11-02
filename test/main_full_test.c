/**
  ******************************************************************************
  * @file    main_full_test.c
  * @brief   FULL HARDWARE TEST - runs every component of the Smart Pill Box
  *          Target: STM32F103C8T6 (Blue Pill)
  *
  * HOW TO RUN
  *   Back up Core/Src/main.c, then replace its contents with this file
  *   (same way as test/main_oled_test.c). Build and flash. No other file changes.
  *
  * WHAT IT TESTS (results are shown on the OLED; LED1 alone = OLED not found)
  *   1. OLED      - all-pixels-on flash, then this text appearing proves it
  *   2. I2C scan  - lists every address that answers
  *   3. RTC       - DS1307 answers and its seconds advance
  *   4. SD card   - init, then write/read-back/restore on scratch block 1010
  *   5. LEDs      - LED1 (PB1) then LED2 (PB10) light up: confirm by eye
  *   6. Buzzer    - PB0 beeps: confirm by ear
  *   7. Buttons   - press B1, B2, B3 within 10 s each
  *   Then a summary page, then a live page (time + button states).
  *
  * The SD test restores the scratch block, but it does write raw sectors
  * (same as the main firmware), so do not use a card holding data you need.
  ******************************************************************************
  */

#include "stm32f1xx_hal.h"
#include "ds1307.h"
#include "sd_card.h"
#include "ssd1306.h"
#include "alarms.h"
#include "buttons.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Set to 0 to skip the SD test (e.g. while the SD card/wiring is being fixed) */
#define RUN_SD_TEST  1

#define SD_SCRATCH_BLOCK  (SD_BLOCK_LOG_START + SD_BLOCK_LOG_COUNT - 1) /* 1010 */

/* SD init diagnostics: weak, so this builds even if sd_card.c/.h are the older version */
extern volatile uint8_t SD_DebugStage __attribute__((weak));
extern volatile uint8_t SD_DebugResp __attribute__((weak));
extern volatile uint8_t SD_DebugSpiTimeouts __attribute__((weak));

I2C_HandleTypeDef hi2c1;

void SystemClock_Config(void);
static void MX_I2C1_Init(void);
void Error_Handler(void);

typedef enum { T_PENDING, T_PASS, T_FAIL } Result_t;
enum { N_OLED, N_RTC, N_SD, N_LED, N_BUZ, N_B1, N_B2, N_B3, N_TESTS };

static const char *const testName[N_TESTS] = {
    "OLED", "RTC", "SD", "LEDs", "BUZZ", "BTN1", "BTN2", "BTN3"
};
static Result_t result[N_TESTS];

/* ---- No frame buffer: run the drawing code once per OLED page ---- */
static void (*bodyFn)(void);

static void renderFrame(void (*fn)(void)) {
    bodyFn = fn;
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_BeginPage(page);
        bodyFn();
        ssd1306_EndPage();
    }
}

/* ---- Screens ---- */
static char line1[24], line2[24], line3[24], title[24];

static void bodyMessage(void) {
    ssd1306_DrawString(2, 2, title, 1, 1, 0);
    ssd1306_DrawLine(0, 12, 127, 12, 1);
    ssd1306_DrawString(2, 20, line1, 1, 1, 0);
    ssd1306_DrawString(2, 34, line2, 1, 1, 0);
    ssd1306_DrawString(2, 48, line3, 1, 1, 0);
}

static void showMessage(const char *t, const char *a, const char *b, const char *c) {
    snprintf(title, sizeof(title), "%s", t);
    snprintf(line1, sizeof(line1), "%s", a);
    snprintf(line2, sizeof(line2), "%s", b);
    snprintf(line3, sizeof(line3), "%s", c);
    renderFrame(bodyMessage);
}

static void bodySummary(void) {
    ssd1306_DrawString(2, 2, "TEST SUMMARY", 1, 1, 0);
    ssd1306_DrawLine(0, 12, 127, 12, 1);
    for (int i = 0; i < N_TESTS; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s:%s", testName[i], result[i] == T_PASS ? "OK" : "FAIL");
        ssd1306_DrawString((i % 2) * 66 + 2, 16 + (i / 2) * 11, buf, 1, 1, 0);
    }
}

static RTC_DateTime_t liveRtc;
static bool liveB1, liveB2, liveB3;

static void bodyLive(void) {
    char buf[24];
    ssd1306_DrawString(2, 2, "LIVE (all tests done)", 1, 1, 0);
    ssd1306_DrawLine(0, 12, 127, 12, 1);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", liveRtc.hours, liveRtc.minutes, liveRtc.seconds);
    ssd1306_DrawString(20, 20, buf, 2, 1, 0);
    snprintf(buf, sizeof(buf), "B1:%c B2:%c B3:%c", liveB1 ? '#' : '-', liveB2 ? '#' : '-', liveB3 ? '#' : '-');
    ssd1306_DrawString(2, 46, buf, 1, 1, 0);
}

/* ---- Individual tests ---- */
static bool buttonHeld(uint16_t pin) {
    return HAL_GPIO_ReadPin(BUTTON_PORT, pin) == GPIO_PIN_RESET;
}

static void testI2CScan(void) {
    uint8_t found[8];
    uint8_t n = 0;
    for (uint16_t addr = 1; addr < 128 && n < 8; addr++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 2, 10) == HAL_OK) {
            found[n++] = (uint8_t)addr;
        }
    }
    char a[24] = "", b[24] = "";
    for (uint8_t i = 0; i < n; i++) {
        char t[8];
        snprintf(t, sizeof(t), "%02X ", found[i]);
        strncat(i < 4 ? a : b, t, sizeof(a) - strlen(i < 4 ? a : b) - 1);
    }
    char count[24];
    snprintf(count, sizeof(count), "found: %d (3C=OLED 68=RTC)", n);
    showMessage("I2C SCAN", n ? a : "none", b, count);
    HAL_Delay(2500);
}

static Result_t testRTC(void) {
    RTC_DateTime_t t1, t2;
    if (DS1307_Init(&hi2c1) != HAL_OK || DS1307_GetTime(&hi2c1, &t1) != HAL_OK) {
        showMessage("RTC", "No answer at 0x68", "check SDA/SCL/power", "");
        HAL_Delay(2500);
        return T_FAIL;
    }
    HAL_Delay(1300);
    if (DS1307_GetTime(&hi2c1, &t2) != HAL_OK) {
        showMessage("RTC", "Second read failed", "", "");
        HAL_Delay(2500);
        return T_FAIL;
    }
    char a[24], b[24];
    snprintf(a, sizeof(a), "t1 %02d:%02d:%02d", t1.hours, t1.minutes, t1.seconds);
    snprintf(b, sizeof(b), "t2 %02d:%02d:%02d", t2.hours, t2.minutes, t2.seconds);
    bool ticking = (t1.seconds != t2.seconds);
    showMessage("RTC", a, b, ticking ? "clock is running" : "STOPPED (battery?)");
    HAL_Delay(2500);
    return ticking ? T_PASS : T_FAIL;
}

static Result_t testSD(void) {
    static uint8_t saved[512], pattern[512], readback[512];
    showMessage("SD CARD", "initialising...", "", "");

    if (!SD_Init()) {
        char d1[24], d2[24];
        if (&SD_DebugStage != NULL) {
            snprintf(d1, sizeof(d1), "stage %d resp %02X", SD_DebugStage, SD_DebugResp);
            snprintf(d2, sizeof(d2), "spi timeouts %d", SD_DebugSpiTimeouts);
        } else {
            snprintf(d1, sizeof(d1), "update sd_card.c/.h");
            snprintf(d2, sizeof(d2), "for more detail");
        }
        showMessage("SD INIT FAILED", d1, d2, "resp FF=no card");
        HAL_Delay(2500);
        return T_FAIL;
    }
    bool haveSaved = SD_ReadBlock(SD_SCRATCH_BLOCK, saved);
    for (int i = 0; i < 512; i++) pattern[i] = (uint8_t)(i * 7 + 0x5A);

    bool wrote = SD_WriteBlock(SD_SCRATCH_BLOCK, pattern);
    bool read  = wrote && SD_ReadBlock(SD_SCRATCH_BLOCK, readback);
    bool match = read && (memcmp(pattern, readback, 512) == 0);

    if (haveSaved) SD_WriteBlock(SD_SCRATCH_BLOCK, saved); /* put the sector back */

    showMessage("SD CARD", wrote ? "write: OK" : "write: FAIL",
                read ? "read:  OK" : "read:  FAIL",
                match ? "data matches" : "DATA MISMATCH");
    HAL_Delay(2500);
    return match ? T_PASS : T_FAIL;
}

static Result_t testLedsAndBuzzer(Result_t *buzzer) {
    showMessage("LEDS", "LED1 (PB1) ON", "watch the LED", "");
    Alarms_Update(true, true, false);   /* LED1 + buzzer on */
    HAL_Delay(1200);
    Alarms_Stop();
    showMessage("LEDS", "LED2 (PB10) ON", "watch the LED", "");
    Alarms_Update(true, false, false);  /* LED2 + buzzer on */
    HAL_Delay(1200);
    Alarms_Stop();
    showMessage("LEDS / BUZZER", "B1 = saw LED & heard", "B3 = something missing", "");

    /* One answer for both: B1 = all fine, B3 = something did not work. */
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 10000) {
        if (buttonHeld(BUTTON_1_PIN)) { *buzzer = T_PASS; HAL_Delay(300); return T_PASS; }
        if (buttonHeld(BUTTON_3_PIN)) { *buzzer = T_FAIL; HAL_Delay(300); return T_FAIL; }
    }
    *buzzer = T_FAIL;
    return T_FAIL;
}

static Result_t waitButton(uint16_t pin, const char *name) {
    char a[24];
    snprintf(a, sizeof(a), "Press %s", name);
    showMessage("BUTTONS", a, "within 10 seconds", "");
    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < 10000) {
        if (buttonHeld(pin)) {
            while (buttonHeld(pin)) HAL_Delay(10);   /* wait for release */
            showMessage("BUTTONS", name, "OK", "");
            HAL_Delay(600);
            return T_PASS;
        }
        HAL_Delay(5);
    }
    showMessage("BUTTONS", name, "not pressed: FAIL", "");
    HAL_Delay(1200);
    return T_FAIL;
}

/* ==============================================================================
 * MAIN
 * ============================================================================== */
int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_I2C1_Init();
    Alarms_Init();
    Buttons_Init();

    /* 1. OLED: ssd1306_Init flashes every pixel for 1.5 s, then clears */
    bool oledPresent = (HAL_I2C_IsDeviceReady(&hi2c1, SSD1306_I2C_ADDR, 3, 50) == HAL_OK)
                    || (HAL_I2C_IsDeviceReady(&hi2c1, SSD1306_ALT_ADDR, 3, 50) == HAL_OK);
    ssd1306_Init(&hi2c1);
    if (!oledPresent) {
        HAL_GPIO_WritePin(LED_PORT, LED_1_PIN, GPIO_PIN_SET);  /* LED1 alone = no OLED */
        while (1) { }
    }
    showMessage("FULL HARDWARE TEST", "If you can read this", "the OLED works.", "starting...");
    result[N_OLED] = T_PASS;
    HAL_Delay(2000);

    /* 2. I2C bus scan (informational) */
    testI2CScan();

    /* 3. RTC */
    result[N_RTC] = testRTC();

    /* 4. SD card */
#if RUN_SD_TEST
    result[N_SD] = testSD();
#else
    result[N_SD] = T_FAIL; /* skipped */
#endif

    /* 5. + 6. LEDs and buzzer */
    result[N_LED] = testLedsAndBuzzer(&result[N_BUZ]);

    /* 7. Buttons */
    result[N_B1] = waitButton(BUTTON_1_PIN, "BUTTON 1");
    result[N_B2] = waitButton(BUTTON_2_PIN, "BUTTON 2");
    result[N_B3] = waitButton(BUTTON_3_PIN, "BUTTON 3");

    /* Summary, then live view */
    renderFrame(bodySummary);
    HAL_Delay(6000);

    while (1) {
        if (DS1307_GetTime(&hi2c1, &liveRtc) != HAL_OK) {
            memset(&liveRtc, 0, sizeof(liveRtc));
        }
        liveB1 = buttonHeld(BUTTON_1_PIN);
        liveB2 = buttonHeld(BUTTON_2_PIN);
        liveB3 = buttonHeld(BUTTON_3_PIN);
        HAL_GPIO_WritePin(LED_PORT, LED_1_PIN, liveB1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_PORT, LED_2_PIN, liveB2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, liveB3 ? GPIO_PIN_SET : GPIO_PIN_RESET);
        renderFrame(bodyLive);
    }
}

/* ==============================================================================
 * CLOCK / I2C (same as the main firmware)
 * ============================================================================== */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
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

void Error_Handler(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    while (1) {
        HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_1);
        for (volatile int i = 0; i < 200000; i++);
    }
}
