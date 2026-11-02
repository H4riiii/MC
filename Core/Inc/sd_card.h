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

/* Log Event Types */
typedef enum {
    SD_EVENT_DOSE_1_TAKEN,
    SD_EVENT_DOSE_2_TAKEN,
    SD_EVENT_WRONG_BUTTON,
    SD_EVENT_CLOCK_ADJUSTED,
    SD_EVENT_CONFIG_UPDATED
} SD_LogEvent_t;

/* Status & API */
bool SD_Init(void);
bool SD_IsAvailable(void);

/* Persistent Config API */
bool SD_LoadConfig(int *p1_hour, int *p1_min, int *p2_hour, int *p2_min);
bool SD_SaveConfig(int p1_hour, int p1_min, int p2_hour, int p2_min);

/* Event Logging API */
bool SD_LogEvent(SD_LogEvent_t event, uint16_t year, uint8_t month, uint8_t day, 
                 uint8_t hour, uint8_t minute, uint8_t second, const char *extraDetails);

#endif /* INC_SD_CARD_H_ */

