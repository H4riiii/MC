#ifndef INC_DS1307_H_
#define INC_DS1307_H_

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define DS1307_I2C_ADDR       (0x68 << 1) // 0xD0: 8-bit shifted address for STM32 HAL

// Register Addresses
#define DS1307_REG_SECONDS    0x00
#define DS1307_REG_MINUTES    0x01
#define DS1307_REG_HOURS      0x02
#define DS1307_REG_DAY        0x03
#define DS1307_REG_DATE       0x04
#define DS1307_REG_MONTH      0x05
#define DS1307_REG_YEAR       0x06
#define DS1307_REG_CONTROL    0x07

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t dayOfWeek; // 1 = Sunday, ..., 7 = Saturday
    uint8_t date;      // 1 - 31
    uint8_t month;     // 1 - 12
    uint16_t year;     // e.g. 2026
} RTC_DateTime_t;

// BCD conversion helpers
#define BCD_TO_DEC(val) (((val) >> 4) * 10 + ((val) & 0x0F))
#define DEC_TO_BCD(val) ((((val) / 10) << 4) | ((val) % 10))

HAL_StatusTypeDef DS1307_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef DS1307_GetTime(I2C_HandleTypeDef *hi2c, RTC_DateTime_t *dt);
HAL_StatusTypeDef DS1307_SetTime(I2C_HandleTypeDef *hi2c, const RTC_DateTime_t *dt);

#endif /* INC_DS1307_H_ */

