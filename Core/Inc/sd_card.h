/**
 * @file sd_card.h
 * @brief Lightweight SPI SD Card driver for logging and config persistence
 *        Hardware: STM32F103 SPI1 (PA4: CS, PA5: SCK, PA6: MISO, PA7: MOSI)
 */

#ifndef INC_SD_CARD_H_
#define INC_SD_CARD_H_

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* SPI Pin Allocation */
#define SD_CS_PORT          GPIOA
#define SD_CS_PIN           GPIO_PIN_4
#define SD_SPI_PORT         GPIOA
#define SD_SCK_PIN          GPIO_PIN_5
#define SD_MISO_PIN         GPIO_PIN_6
#define SD_MOSI_PIN         GPIO_PIN_7

/* Reserved SD Storage Blocks */
#define SD_BLOCK_CONFIG     1000
#define SD_BLOCK_LOG_START  1001
#define SD_BLOCK_LOG_COUNT  10

/* Log Event Types */
typedef enum {
    SD_EVENT_DOSE_1_TAKEN,
    SD_EVENT_DOSE_2_TAKEN,
    SD_EVENT_WRONG_BUTTON,
    SD_EVENT_CLOCK_ADJUSTED,
    SD_EVENT_CONFIG_UPDATED
} SD_LogEvent_t;

/* Dosage Status for 3-Day History */
typedef enum {
    DOSE_STATUS_EMPTY = 0,
    DOSE_STATUS_PENDING,
    DOSE_STATUS_TAKEN,
    DOSE_STATUS_MISSED
} DoseRecordStatus_t;

/* Daily Dosage Entry for 3-Day History */
typedef struct {
    uint8_t day;
    uint8_t month;
    uint16_t year;
    DoseRecordStatus_t p1_status;
    uint8_t p1_hour;
    uint8_t p1_min;
    DoseRecordStatus_t p2_status;
    uint8_t p2_hour;
    uint8_t p2_min;
} DailyDoseLog_t;

/* Status & API */
bool SD_Init(void);
/* Init diagnostics: stage 1-6 reached, last response byte, SPI wait time-outs */
extern volatile uint8_t SD_DebugStage, SD_DebugResp, SD_DebugSpiTimeouts;
bool SD_IsAvailable(void);

/* Low-level Block Operations */
bool SD_ReadBlock(uint32_t blockNum, uint8_t *buffer);
bool SD_WriteBlock(uint32_t blockNum, const uint8_t *buffer);

/* Persistent Config API (Saved to SD Card Sector 1000) */
bool SD_LoadConfig(int *p1_hour, int *p1_min, int *p2_hour, int *p2_min);
bool SD_SaveConfig(int p1_hour, int p1_min, int p2_hour, int p2_min);

/* Event Logging API (Saved to SD Card Sector 1001+) */
bool SD_LogEvent(SD_LogEvent_t event, uint16_t year, uint8_t month, uint8_t day, 
                 uint8_t hour, uint8_t minute, uint8_t second, const char *extraDetails);

/* 3-Day Dosage History Management */
void DoseHistory_Init(DailyDoseLog_t history[3], uint16_t curYear, uint8_t curMonth, uint8_t curDay,
                      int p1_hour, int p1_min, int p2_hour, int p2_min, int curHour, int curMin);
void DoseHistory_RecordDose(DailyDoseLog_t history[3], int pillNum, uint8_t hour, uint8_t minute);
void DoseHistory_UpdateStatus(DailyDoseLog_t history[3], int p1_hour, int p1_min, int p2_hour, int p2_min,
                              int curHour, int curMin);
void DoseHistory_CheckDayChange(DailyDoseLog_t history[3], uint16_t curYear, uint8_t curMonth, uint8_t curDay,
                                int p1_hour, int p1_min, int p2_hour, int p2_min);

#endif /* INC_SD_CARD_H_ */
