/**
  ******************************************************************************
  * @file    ui_screens.c
  * @brief   OLED Graphic Screens & Menu UI Renderer implementation
  *          Target: STM32F103C8T6 (Blue Pill)
  ******************************************************************************
  */

#include "ui_screens.h"
#include "ssd1306.h"
#include <stdio.h>

static void printHeader(const UI_Context_t *ctx) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ctx->rtc.hours, ctx->rtc.minutes, ctx->rtc.seconds);
    ssd1306_DrawString(38, 2, buf, 1, 1, 0);

    // Draw indicator if RTC is actively responding
    if (!ctx->rtcAvailable) {
        ssd1306_DrawString(2, 2, "!", 1, 1, 0); // Warning icon if RTC read failed
    }
    if (ctx->sdAvailable) {
        ssd1306_DrawString(110, 2, "SD", 1, 1, 0);
    }
    ssd1306_DrawLine(0, 12, 127, 12, 1);
}

static void renderBody(const UI_Context_t *ctx) {
    ssd1306_Clear(0);

    switch (ctx->state) {
        case NORMAL_IDLE: {
            printHeader(ctx);
            char buf[16];
            ssd1306_DrawString(4, 18, "P1: ", 1, 1, 0);
            snprintf(buf, sizeof(buf), "%02d:%02d", ctx->dose1.hour, ctx->dose1.minute);
            ssd1306_DrawString(28, 18, buf, 1, 1, 0);

            ssd1306_DrawString(68, 18, "P2: ", 1, 1, 0);
            snprintf(buf, sizeof(buf), "%02d:%02d", ctx->dose2.hour, ctx->dose2.minute);
            ssd1306_DrawString(92, 18, buf, 1, 1, 0);

            ssd1306_DrawLine(0, 30, 127, 30, 1);
            ssd1306_DrawString(12, 36, "READY & MONITORING", 1, 1, 0);
            ssd1306_DrawString(10, 52, "Press [B3] for Setup", 1, 1, 0);
            break;
        }

        case REMINDER_DOSE_1:
        case REMINDER_DOSE_2: {
            printHeader(ctx);
            bool isDose1 = (ctx->state == REMINDER_DOSE_1);

            if (ctx->wrongButtonPressed) {
                ssd1306_FillRect(0, 16, 128, 48, ctx->alarmVisualState ? 1 : 0);
                uint8_t fg = ctx->alarmVisualState ? 0 : 1;
                uint8_t bg = ctx->alarmVisualState ? 1 : 0;

                ssd1306_DrawString(10, 20, "WRONG PILL!", 2, fg, bg);
                ssd1306_DrawString(10, 40, isDose1 ? "Press BUTTON 1" : "Press BUTTON 2", 1, fg, bg);
                ssd1306_DrawString(10, 52, isDose1 ? "for PILL 1" : "for PILL 2", 1, fg, bg);
            } else {
                if (ctx->alarmVisualState) {
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
            ssd1306_DrawString(34, 2, "SETTINGS", 1, 1, 0);
            ssd1306_DrawLine(0, 11, 127, 11, 1);

            ssd1306_DrawString(8, 14, (ctx->setupMenuItem == 1) ? "> 1. PILL 1 TIME" : "  1. PILL 1 TIME", 1, 1, 0);
            ssd1306_DrawString(8, 24, (ctx->setupMenuItem == 2) ? "> 2. PILL 2 TIME" : "  2. PILL 2 TIME", 1, 1, 0);
            ssd1306_DrawString(8, 34, (ctx->setupMenuItem == 3) ? "> 3. ADJUST CLOCK" : "  3. ADJUST CLOCK", 1, 1, 0);
            ssd1306_DrawString(8, 44, (ctx->setupMenuItem == 4) ? "> 4. VIEW LOGS"    : "  4. VIEW LOGS",    1, 1, 0);

            ssd1306_DrawLine(0, 54, 127, 54, 1);
            ssd1306_DrawString(2, 56, "B1:Next B2:Select B3:Exit", 1, 1, 0);
            break;
        }

        case SETUP_EDIT_HOUR:
        case SETUP_EDIT_MINUTE: {
            if (ctx->setupMenuItem == 1) {
                ssd1306_DrawString(24, 2, "PILL 1 TIME", 1, 1, 0);
            } else if (ctx->setupMenuItem == 2) {
                ssd1306_DrawString(24, 2, "PILL 2 TIME", 1, 1, 0);
            } else {
                ssd1306_DrawString(24, 2, "ADJUST CLOCK", 1, 1, 0);
            }
            ssd1306_DrawLine(0, 12, 127, 12, 1);

            char buf[8];
            bool isEditHour = (ctx->state == SETUP_EDIT_HOUR);

            if (isEditHour && !ctx->cursorVisible) {
                ssd1306_DrawString(30, 24, "  ", 2, 1, 0);
            } else {
                snprintf(buf, sizeof(buf), "%02d", ctx->tempHour);
                ssd1306_DrawString(30, 24, buf, 2, 1, 0);
            }

            ssd1306_DrawString(56, 24, ":", 2, 1, 0);

            if (!isEditHour && !ctx->cursorVisible) {
                ssd1306_DrawString(70, 24, "  ", 2, 1, 0);
            } else {
                snprintf(buf, sizeof(buf), "%02d", ctx->tempMinute);
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

            ssd1306_DrawString(30, 24, ctx->confirmSaveChoice ? "> YES, SAVE" : "  YES, SAVE", 1, 1, 0);
            ssd1306_DrawString(30, 38, !ctx->confirmSaveChoice ? "> CANCEL" : "  CANCEL", 1, 1, 0);

            ssd1306_DrawLine(0, 52, 127, 52, 1);
            ssd1306_DrawString(2, 55, "B1:Toggle  B2:Confirm", 1, 1, 0);
            break;
        }

        case SETUP_FEEDBACK: {
            if (ctx->setupMenuItem == 1) {
                ssd1306_DrawString(16, 12, "PILL 1", 2, 1, 0);
            } else if (ctx->setupMenuItem == 2) {
                ssd1306_DrawString(16, 12, "PILL 2", 2, 1, 0);
            } else {
                ssd1306_DrawString(16, 12, "RTC CLOCK", 2, 1, 0);
            }
            ssd1306_DrawString(20, 38, ctx->confirmSaveChoice ? "TIME SAVED!" : "DISCARDED", 1, 1, 0);
            break;
        }

        case VIEW_LOGS: {
            if (ctx->doseHistory == NULL) break;
            char buf[48];
            const char *tag = (ctx->viewLogDay == 0) ? "TODAY" : ((ctx->viewLogDay == 1) ? "-1 DAY" : "-2 DAYS");
            snprintf(buf, sizeof(buf), "LOG [%d/3] %02d/%02d (%s)", 
                     ctx->viewLogDay + 1, ctx->doseHistory[ctx->viewLogDay].day, ctx->doseHistory[ctx->viewLogDay].month, tag);
            ssd1306_DrawString(2, 2, buf, 1, 1, 0);
            ssd1306_DrawLine(0, 12, 127, 12, 1);

            // Pill 1 Line
            if (ctx->doseHistory[ctx->viewLogDay].p1_status == DOSE_STATUS_TAKEN) {
                snprintf(buf, sizeof(buf), "P1[%02d:%02d]: OK %02d:%02d", 
                         ctx->dose1.hour, ctx->dose1.minute, 
                         ctx->doseHistory[ctx->viewLogDay].p1_hour, ctx->doseHistory[ctx->viewLogDay].p1_min);
            } else if (ctx->doseHistory[ctx->viewLogDay].p1_status == DOSE_STATUS_MISSED) {
                snprintf(buf, sizeof(buf), "P1[%02d:%02d]: MISSED", ctx->dose1.hour, ctx->dose1.minute);
            } else {
                snprintf(buf, sizeof(buf), "P1[%02d:%02d]: PENDING", ctx->dose1.hour, ctx->dose1.minute);
            }
            ssd1306_DrawString(2, 16, buf, 1, 1, 0);

            // Pill 2 Line
            if (ctx->doseHistory[ctx->viewLogDay].p2_status == DOSE_STATUS_TAKEN) {
                snprintf(buf, sizeof(buf), "P2[%02d:%02d]: OK %02d:%02d", 
                         ctx->dose2.hour, ctx->dose2.minute, 
                         ctx->doseHistory[ctx->viewLogDay].p2_hour, ctx->doseHistory[ctx->viewLogDay].p2_min);
            } else if (ctx->doseHistory[ctx->viewLogDay].p2_status == DOSE_STATUS_MISSED) {
                snprintf(buf, sizeof(buf), "P2[%02d:%02d]: MISSED", ctx->dose2.hour, ctx->dose2.minute);
            } else {
                snprintf(buf, sizeof(buf), "P2[%02d:%02d]: PENDING", ctx->dose2.hour, ctx->dose2.minute);
            }
            ssd1306_DrawString(2, 28, buf, 1, 1, 0);

            // SD storage sector line
            ssd1306_DrawLine(0, 40, 127, 40, 1);
            ssd1306_DrawString(2, 43, ctx->sdAvailable ? "SD: SAVED [SEC 1001]" : "SD: NOT DETECTED", 1, 1, 0);

            // Navigation footer
            ssd1306_DrawLine(0, 53, 127, 53, 1);
            ssd1306_DrawString(2, 55, "B1:Next Day   B3:Back", 1, 1, 0);
            break;
        }
    }

}

void UI_RenderScreen(const UI_Context_t *ctx) {
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_BeginPage(page);
        renderBody(ctx);
        ssd1306_EndPage();
    }
}

static void splashBody(const uint8_t *foundDevices, uint8_t foundCount, bool rtcOk, bool sdOk) {
    ssd1306_Clear(0);
    ssd1306_DrawString(8, 4, "I2C BUS SCAN:", 1, 1, 0);

    char scanMsg[32];
    if (foundCount == 0) {
        ssd1306_DrawString(8, 20, "NO DEVICES FOUND", 1, 1, 0);
    } else {
        int xOffset = 8;
        int yOffset = 20;
        for (uint8_t i = 0; i < foundCount; i++) {
            snprintf(scanMsg, sizeof(scanMsg), "0x%02X ", foundDevices[i]);
            ssd1306_DrawString(xOffset, yOffset, scanMsg, 1, 1, 0);
            xOffset += 32;
            if (xOffset > 100) {
                xOffset = 8;
                yOffset += 12;
            }
        }
    }

    ssd1306_DrawString(8, 44, rtcOk ? "RTC: OK" : "RTC: NONE", 1, 1, 0);
    ssd1306_DrawString(70, 44, sdOk ? "SD: OK" : "SD: NONE", 1, 1, 0);
}

void UI_DrawI2CScanSplash(const uint8_t *foundDevices, uint8_t foundCount, bool rtcOk, bool sdOk) {
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_BeginPage(page);
        splashBody(foundDevices, foundCount, rtcOk, sdOk);
        ssd1306_EndPage();
    }
}

