/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : SMART PILL BOX - MAIN APPLICATION ENTRY POINT
  *                   Target: STM32F103C8T6 (Blue Pill)
  *
  * PINOUT SUMMARY:
  *   - I2C1 (PB6 = SCL, PB7 = SDA) -> SSD1306 OLED & DS1307 RTC
  *   - SPI1 (PA4=CS, PA5=SCK, PA6=MISO, PA7=MOSI) -> MicroSD Card Module
  *   - Push Buttons (PB12=Btn1, PB14=Btn2, PB15=Btn3) -> Active LOW Pull-up
  *   - Indicators (PB1=LED1, PB10=LED2, PB0=Buzzer)
  ******************************************************************************
  */

#include "stm32f1xx_hal.h"
#include "ds1307.h"
#include "sd_card.h"
#include "ssd1306.h"
#include "alarms.h"
#include "buttons.h"
#include "ui_screens.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ==============================================================================
 * TIMING CONSTANTS (Milliseconds)
 * ============================================================================== */
#define NORMAL_BLINK_INTERVAL  500
#define FAST_BLINK_INTERVAL    150
#define FIELD_BLINK_INTERVAL   400

/* ==============================================================================
 * GLOBAL SYSTEM VARIABLES
 * ============================================================================== */
I2C_HandleTypeDef hi2c1;

// System state & display triggers
static SystemState_t currentState = NORMAL_IDLE;
static bool updateScreen = true;

// Scheduled dose alarms: Dose 1 at 07:30, Dose 2 at 19:00
static DoseAlarm_t dose1 = {7, 30, -1};
static DoseAlarm_t dose2 = {19, 0, -1};

// 3-Day Dosage History Tracker
static DailyDoseLog_t doseHistory[3];
static int viewLogDay = 0; // 0 = Today, 1 = Yesterday, 2 = Day Before

// Setup temporary edit variables
static int setupMenuItem = 1;
static int tempHour = 0;
static int tempMinute = 0;
static bool confirmSaveChoice = true;
static uint32_t feedbackTimer = 0;
static uint32_t doseConfirmedTimer = 0;
static bool sdAvailable = false;

// Alarm & UI tracking
static uint32_t lastAlarmToggle = 0;
static uint32_t currentBlinkInterval = NORMAL_BLINK_INTERVAL;
static bool alarmVisualState = false;
static bool wrongButtonPressed = false;

static uint32_t lastBlinkToggle = 0;
static uint32_t lastRtcRead = 0;
static bool cursorVisible = true;
static uint8_t lastDisplayedSecond = 0xFF; // Tracks last rendered second; 0xFF forces immediate redraw

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

/* Forward Declarations */
void SystemClock_Config(void);
static void MX_I2C1_Init(void);
void Error_Handler(void);

/* ==============================================================================
 * MAIN FUNCTION & APPLICATION LOOP
 * ============================================================================== */
int main(void) {
    // 1. STM32 Low-Level HAL & Clock Initialization
    HAL_Init();
    SystemClock_Config();

    // 2. Initialize Hardware Peripherals & Modules
    MX_I2C1_Init();
    Alarms_Init();
    Buttons_Init();

    // 3. Initialize SSD1306 OLED Display (Zero-Grain init)
    ssd1306_Init(&hi2c1);

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

    // 6. I2C Bus Scan & Diagnostic Splash Screen
    uint8_t foundDevices[8] = {0};
    uint8_t foundCount = 0;
    for (uint16_t addr = 1; addr < 128; addr++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 2, 10) == HAL_OK) {
            if (foundCount < 8) {
                foundDevices[foundCount++] = (uint8_t)addr;
            }
        }
    }

    UI_DrawI2CScanSplash(foundDevices, foundCount, rtcAvailable, sdAvailable);
    HAL_Delay(2500);

    // 7. Initialize 3-Day Dosage History tracking
    DoseHistory_Init(doseHistory, currentRtc.year, currentRtc.month, currentRtc.date,
                     dose1.hour, dose1.minute, dose2.hour, dose2.minute, currentRtc.hours, currentRtc.minutes);

    currentState = NORMAL_IDLE;
    updateScreen = true;
    lastRtcRead = HAL_GetTick();

    /* ==========================================================================
     * APPLICATION SUPER-LOOP
     * ========================================================================== */
    while (1) {
        uint32_t now = HAL_GetTick();

        // 1. Read real time from DS1307 RTC (every 500 ms)
        if (now - lastRtcRead >= 500) {
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
                    // Physical 32.768 kHz oscillator stalled: kick-start and tick forward
                    DS1307_Init(&hi2c1);
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
            // Fix 1: Only redraw in NORMAL_IDLE when the second digit actually changes.
            // All menu/alarm states are event-driven (buttons + blink timer) — skip their 500ms redraw.
            if (currentState == NORMAL_IDLE && currentRtc.seconds != lastDisplayedSecond) {
                lastDisplayedSecond = currentRtc.seconds;
                updateScreen = true;
            }
        }

        // 2. Read Active LOW buttons with edge-detection debounce
        bool btn1Pressed = false;
        bool btn2Pressed = false;
        bool btn3Pressed = false;
        Buttons_Scan(now, &btn1Pressed, &btn2Pressed, &btn3Pressed);

        if (btn1Pressed || btn2Pressed || btn3Pressed) {
            updateScreen = true;
        }

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
                    setupMenuItem = 1;
                    updateScreen = true;
                }

                // Check Pill 1 Alarm Trigger against Real RTC Time
                if (currentRtc.hours == dose1.hour && currentRtc.minutes == dose1.minute && dose1.lastTriggeredDay != currentRtc.date) {
                    dose1.lastTriggeredDay = currentRtc.date;
                    alarmVisualState = false;
                    wrongButtonPressed = false;
                    currentBlinkInterval = NORMAL_BLINK_INTERVAL;
                    currentState = REMINDER_DOSE_1;
                    updateScreen = true;
                }

                // Check Pill 2 Alarm Trigger against Real RTC Time
                if (currentRtc.hours == dose2.hour && currentRtc.minutes == dose2.minute && dose2.lastTriggeredDay != currentRtc.date) {
                    dose2.lastTriggeredDay = currentRtc.date;
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
                    Alarms_Update(alarmVisualState, true, wrongButtonPressed);
                    updateScreen = true;
                }

                // Button 1 is CORRECT for Pill 1
                if (btn1Pressed) {
                    Alarms_Stop();
                    doseConfirmedTimer = now;
                    currentState = DOSE_CONFIRMED;
                    DoseHistory_RecordDose(doseHistory, 1, currentRtc.hours, currentRtc.minutes);
                    SD_LogEvent(SD_EVENT_DOSE_1_TAKEN, currentRtc.year, currentRtc.month, currentRtc.date,
                                currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Confirmed Button 1");
                    updateScreen = true;
                }
                // Button 2 is WRONG for Pill 1
                else if (btn2Pressed && !wrongButtonPressed) {
                    wrongButtonPressed = true;
                    currentBlinkInterval = FAST_BLINK_INTERVAL;
                    SD_LogEvent(SD_EVENT_WRONG_BUTTON, currentRtc.year, currentRtc.month, currentRtc.date,
                                currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Pill 1 due - Pressed Btn 2");
                    updateScreen = true;
                }
                break;

            case REMINDER_DOSE_2:
                if (now - lastAlarmToggle >= currentBlinkInterval) {
                    lastAlarmToggle = now;
                    alarmVisualState = !alarmVisualState;
                    Alarms_Update(alarmVisualState, false, wrongButtonPressed);
                    updateScreen = true;
                }

                // Button 2 is CORRECT for Pill 2
                if (btn2Pressed) {
                    Alarms_Stop();
                    doseConfirmedTimer = now;
                    currentState = DOSE_CONFIRMED;
                    DoseHistory_RecordDose(doseHistory, 2, currentRtc.hours, currentRtc.minutes);
                    SD_LogEvent(SD_EVENT_DOSE_2_TAKEN, currentRtc.year, currentRtc.month, currentRtc.date,
                                currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Confirmed Button 2");
                    updateScreen = true;
                }
                // Button 1 is WRONG for Pill 2
                else if (btn1Pressed && !wrongButtonPressed) {
                    wrongButtonPressed = true;
                    currentBlinkInterval = FAST_BLINK_INTERVAL;
                    SD_LogEvent(SD_EVENT_WRONG_BUTTON, currentRtc.year, currentRtc.month, currentRtc.date,
                                currentRtc.hours, currentRtc.minutes, currentRtc.seconds, "Pill 2 due - Pressed Btn 1");
                    updateScreen = true;
                }
                break;

            case DOSE_CONFIRMED:
                if (now - doseConfirmedTimer >= 2000) {
                    currentState = NORMAL_IDLE;
                    lastDisplayedSecond = 0xFF; // Force clock face redraw on return
                    updateScreen = true;
                }
                break;

            case SETUP_MENU:
                if (btn1Pressed) {
                    setupMenuItem++;
                    if (setupMenuItem > 4) setupMenuItem = 1;
                    updateScreen = true;
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
                    updateScreen = true;
                }
                if (btn3Pressed) {
                    currentState = NORMAL_IDLE;
                    lastDisplayedSecond = 0xFF; // Force clock face redraw on return
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
                    updateScreen = true;
                }
                break;

            case SETUP_FEEDBACK:
                if (now - feedbackTimer >= 1500) {
                    currentState = NORMAL_IDLE;
                    lastDisplayedSecond = 0xFF; // Force clock face redraw on return
                    updateScreen = true;
                }
                break;

            case VIEW_LOGS:
                if (btn1Pressed) {
                    viewLogDay = (viewLogDay + 1) % 3;
                    updateScreen = true;
                }
                if (btn3Pressed) {
                    currentState = SETUP_MENU;
                    updateScreen = true;
                }
                break;
        }

        // 5. Redraw OLED screen when state changes
        if (updateScreen) {
            UI_Context_t uiCtx = {
                .state = currentState,
                .rtc = currentRtc,
                .rtcAvailable = rtcAvailable,
                .sdAvailable = sdAvailable,
                .dose1 = dose1,
                .dose2 = dose2,
                .doseHistory = doseHistory,
                .viewLogDay = viewLogDay,
                .setupMenuItem = setupMenuItem,
                .tempHour = tempHour,
                .tempMinute = tempMinute,
                .confirmSaveChoice = confirmSaveChoice,
                .cursorVisible = cursorVisible,
                .alarmVisualState = alarmVisualState,
                .wrongButtonPressed = wrongButtonPressed
            };
            UI_RenderScreen(&uiCtx);
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

    // Pins PB6/PB7 are configured by HAL_I2C_MspInit (stm32f1xx_hal_msp.c), as in a335f8b
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
    // Diagnostic visual panic blink on Pill 1 LED (PB1) instead of silent freeze
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
