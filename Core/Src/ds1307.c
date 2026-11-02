#include "ds1307.h"

HAL_StatusTypeDef DS1307_Init(I2C_HandleTypeDef *hi2c) {
    // 1. Check if the DS1307 is reachable on the bus (7-bit address 0x68)
    if (HAL_I2C_IsDeviceReady(hi2c, DS1307_I2C_ADDR, 2, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    // 2. Read current 7 timekeeper registers
    uint8_t buf[7];
    if (HAL_I2C_Mem_Read(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, buf, 7, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    // If Clock Halt bit 7 is set (oscillator stopped), clear it to start oscillator,
    // while strictly preserving the user's configured hours, minutes, and seconds!
    if (buf[0] & 0x80) {
        buf[0] &= 0x7F; // Clear CH bit
        HAL_I2C_Mem_Write(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, &buf[0], 1, 100);
    }

    // Ensure 24-hour mode (bit 6 of hours register must be 0)
    if (buf[2] & 0x40) {
        uint8_t hr = buf[2] & 0x3F;
        HAL_I2C_Mem_Write(hi2c, DS1307_I2C_ADDR, DS1307_REG_HOURS, 1, &hr, 1, 100);
    }

    // Ensure date/month/year are valid if completely zeroed out
    if ((buf[4] & 0x3F) == 0 || (buf[5] & 0x1F) == 0) {
        uint8_t dateInit[3] = {DEC_TO_BCD(2), DEC_TO_BCD(11), DEC_TO_BCD(26)};
        HAL_I2C_Mem_Write(hi2c, DS1307_I2C_ADDR, DS1307_REG_DATE, 1, dateInit, 3, 100);
    }

    // 3. Ensure Control Register 0x07 has SQWE disabled and output LOW
    uint8_t ctrlReg = 0x00;
    HAL_I2C_Mem_Write(hi2c, DS1307_I2C_ADDR, DS1307_REG_CONTROL, 1, &ctrlReg, 1, 100);

    return HAL_OK;
}

HAL_StatusTypeDef DS1307_GetTime(I2C_HandleTypeDef *hi2c, RTC_DateTime_t *dt) {
    uint8_t buf[7];
    if (HAL_I2C_Mem_Read(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, buf, 7, 100) != HAL_OK) {
        return HAL_ERROR;
    }

    // Bit 7 is CH flag; mask with 0x7F to get seconds
    dt->seconds = BCD_TO_DEC(buf[0] & 0x7F);
    dt->minutes = BCD_TO_DEC(buf[1] & 0x7F);
    
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

    // Bit 7 of register 0x00 MUST be 0 to keep the 32.768 kHz oscillator running
    buf[0] = DEC_TO_BCD(dt->seconds & 0x7F);
    buf[1] = DEC_TO_BCD(dt->minutes % 60);
    buf[2] = DEC_TO_BCD(dt->hours % 24);          // Bit 6 = 0 forces 24-hour mode
    buf[3] = DEC_TO_BCD(dt->dayOfWeek > 0 && dt->dayOfWeek <= 7 ? dt->dayOfWeek : 1);
    buf[4] = DEC_TO_BCD(dt->date > 0 && dt->date <= 31 ? dt->date : 1);
    buf[5] = DEC_TO_BCD(dt->month > 0 && dt->month <= 12 ? dt->month : 1);
    buf[6] = DEC_TO_BCD((dt->year >= 2000) ? (dt->year - 2000) : dt->year);

    return HAL_I2C_Mem_Write(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, buf, 7, 100);
}
