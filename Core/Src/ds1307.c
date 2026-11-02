#include "ds1307.h"

HAL_StatusTypeDef DS1307_Init(I2C_HandleTypeDef *hi2c) {
    uint8_t secReg = 0;
    // Check if the DS1307 is reachable on the bus
    if (HAL_I2C_IsDeviceReady(hi2c, DS1307_I2C_ADDR, 2, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    // Read seconds register to inspect Clock Halt (CH) bit (bit 7)
    if (HAL_I2C_Mem_Read(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, &secReg, 1, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    // If CH bit is 1, oscillator is stopped. Clear bit 7 to start oscillator.
    if (secReg & 0x80) {
        secReg &= 0x7F;
        if (HAL_I2C_Mem_Write(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, &secReg, 1, 100) != HAL_OK) {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef DS1307_GetTime(I2C_HandleTypeDef *hi2c, RTC_DateTime_t *dt) {
    uint8_t buf[7];
    if (HAL_I2C_Mem_Read(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, buf, 7, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    dt->seconds   = BCD_TO_DEC(buf[0] & 0x7F);
    dt->minutes   = BCD_TO_DEC(buf[1] & 0x7F);
    
    // Check 12/24 hour mode bit (Bit 6). DS1307 standard: 0 = 24-hr mode, 1 = 12-hr mode
    if (buf[2] & 0x40) {
        // 12-hour mode
        dt->hours = BCD_TO_DEC(buf[2] & 0x1F);
        if (buf[2] & 0x20) { // PM flag
            dt->hours = (dt->hours % 12) + 12;
        }
    } else {
        // 24-hour mode
        dt->hours = BCD_TO_DEC(buf[2] & 0x3F);
    }

    dt->dayOfWeek = BCD_TO_DEC(buf[3] & 0x07);
    dt->date      = BCD_TO_DEC(buf[4] & 0x3F);
    dt->month     = BCD_TO_DEC(buf[5] & 0x1F);
    dt->year      = 2000 + BCD_TO_DEC(buf[6]);

    return HAL_OK;
}

HAL_StatusTypeDef DS1307_SetTime(I2C_HandleTypeDef *hi2c, const RTC_DateTime_t *dt) {
    uint8_t buf[7];

    buf[0] = DEC_TO_BCD(dt->seconds & 0x7F);       // CH bit = 0 (oscillator running)
    buf[1] = DEC_TO_BCD(dt->minutes % 60);
    buf[2] = DEC_TO_BCD(dt->hours % 24);          // Bit 6 = 0 for 24-hour format
    buf[3] = DEC_TO_BCD(dt->dayOfWeek > 0 && dt->dayOfWeek <= 7 ? dt->dayOfWeek : 1);
    buf[4] = DEC_TO_BCD(dt->date > 0 && dt->date <= 31 ? dt->date : 1);
    buf[5] = DEC_TO_BCD(dt->month > 0 && dt->month <= 12 ? dt->month : 1);
    buf[6] = DEC_TO_BCD((dt->year >= 2000) ? (dt->year - 2000) : dt->year);

    return HAL_I2C_Mem_Write(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, buf, 7, 100);
}

