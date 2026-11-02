/**
  ******************************************************************************
  * @file    ui_screens.h
  * @brief   OLED Graphic Screens & Menu UI Renderer
  *          Target: STM32F103C8T6 (Blue Pill)
  ******************************************************************************
  */

#ifndef INC_UI_SCREENS_H_
#define INC_UI_SCREENS_H_

#include "stm32f1xx_hal.h"
#include "ds1307.h"
#include "sd_card.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* System Finite State Machine States */
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

/* Scheduled Dose Alarm Data */
typedef struct {
    int hour;
    int minute;
    int lastTriggeredDay;
} DoseAlarm_t;

/* Complete UI Display Context */
typedef struct {
    SystemState_t       state;
    RTC_DateTime_t      rtc;
    bool                rtcAvailable;
    bool                sdAvailable;
    DoseAlarm_t         dose1;
    DoseAlarm_t         dose2;
    const DailyDoseLog_t *doseHistory;
    int                 viewLogDay;
    int                 setupMenuItem;
    int                 tempHour;
    int                 tempMinute;
    bool                confirmSaveChoice;
    bool                cursorVisible;
    bool                alarmVisualState;
    bool                wrongButtonPressed;
} UI_Context_t;

/* UI Rendering Functions */
void UI_RenderScreen(const UI_Context_t *ctx);
void UI_DrawI2CScanSplash(const uint8_t *foundDevices, uint8_t foundCount, bool rtcOk, bool sdOk);

#ifdef __cplusplus
}
#endif

#endif /* INC_UI_SCREENS_H_ */

