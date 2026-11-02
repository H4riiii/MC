/**
 * @file sd_card.c
 * @brief SD Card driver implementation over SPI1 (PA4: CS, PA5: SCK, PA6: MISO, PA7: MOSI)
 */

#include "sd_card.h"
#include <stdio.h>
#include <string.h>

static bool sdCardReady = false;
static bool isSDHC = true; // Assume standard block addressing (SDHC/SDXC or block mode)

/* Low-level SPI transmission helper */
static uint8_t SPI1_Transfer(uint8_t data) {
    // Wait until TXE (Transmit buffer empty)
    while (!(SPI1->SR & SPI_SR_TXE));
    SPI1->DR = data;
    // Wait until RXNE (Receive buffer not empty)
    while (!(SPI1->SR & SPI_SR_RXNE));
    return (uint8_t)(SPI1->DR);
}

static void SD_Select(void) {
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
}

static void SD_Deselect(void) {
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    SPI1_Transfer(0xFF); // Provide dummy clocks after deselect
}

static uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg, uint8_t crc) {
    uint8_t res, retry = 0;

    SD_Select();

    // Command packet: [Command index | 0x40] [4 bytes argument] [CRC | 0x01]
    SPI1_Transfer(cmd | 0x40);
    SPI1_Transfer((uint8_t)(arg >> 24));
    SPI1_Transfer((uint8_t)(arg >> 16));
    SPI1_Transfer((uint8_t)(arg >> 8));
    SPI1_Transfer((uint8_t)arg);
    SPI1_Transfer(crc);

    // Wait for response (bit 7 must be 0)
    do {
        res = SPI1_Transfer(0xFF);
        retry++;
    } while ((res & 0x80) && (retry < 200));

    return res;
}

/**
 * @brief Low-level SPI1 and GPIO initialization
 */
static void SD_LowLevel_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // CS Pin (PA4) - Output Push-Pull
    GPIO_InitStruct.Pin = SD_CS_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SD_CS_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);

    // SCK (PA5) and MOSI (PA7) - Alternate Function Push-Pull
    GPIO_InitStruct.Pin = SD_SCK_PIN | SD_MOSI_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SD_SPI_PORT, &GPIO_InitStruct);

    // MISO (PA6) - Input with Pull-up
    GPIO_InitStruct.Pin = SD_MISO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(SD_SPI_PORT, &GPIO_InitStruct);

    // Configure SPI1 Peripheral directly for high reliability & speed
    SPI1->CR1 = 0;
    // Baud rate prescaler /256 initially (~281 kHz for SD init @ 72MHz PCLK2)
    SPI1->CR1 |= (SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0);
    SPI1->CR1 |= SPI_CR1_MSTR; // Master mode
    SPI1->CR1 |= (SPI_CR1_SSM | SPI_CR1_SSI); // Software slave management
    SPI1->CR1 |= SPI_CR1_SPE;  // Enable SPI1
}

/**
 * @brief Initialize the SD Card in SPI mode
 */
bool SD_Init(void) {
    SD_LowLevel_Init();

    // 1. Send at least 74 clock cycles with CS HIGH to enter SPI mode
    SD_Deselect();
    for (int i = 0; i < 10; i++) {
        SPI1_Transfer(0xFF);
    }

    // 2. CMD0 (GO_IDLE_STATE) with valid CRC 0x95
    uint8_t res = SD_SendCommand(0, 0, 0x95);
    SD_Deselect();

    if (res != 0x01) {
        sdCardReady = false;
        return false;
    }

    // 3. CMD8 (SEND_IF_COND) to verify voltage range (2.7V-3.6V)
    res = SD_SendCommand(8, 0x1AA, 0x87);
    if (res == 0x01) {
        // Read 4 bytes return packet
        for (int i = 0; i < 4; i++) { 
            (void)SPI1_Transfer(0xFF); 
        }
    }
    SD_Deselect();

    // 4. Send ACMD41 loop until card is ready (returns 0x00)
    uint32_t timeout = 500;
    do {
        // CMD55 (APP_CMD)
        SD_SendCommand(55, 0, 0x65);
        SD_Deselect();
        // ACMD41 with HCS (High Capacity Support) bit 30 set
        res = SD_SendCommand(41, 0x40000000, 0x77);
        SD_Deselect();
        HAL_Delay(2);
        timeout--;
    } while ((res != 0x00) && (timeout > 0));

    if (res == 0x00) {
        // Check OCR (CMD58) to check if card uses block addressing (CCS bit)
        res = SD_SendCommand(58, 0, 0x00);
        if (res == 0x00) {
            uint8_t ocr[4];
            for (int i = 0; i < 4; i++) {
                ocr[i] = SPI1_Transfer(0xFF);
            }
            isSDHC = (ocr[0] & 0x40) ? true : false;
        } else {
            isSDHC = true;
        }
        SD_Deselect();

        sdCardReady = true;
        // Speed up SPI1 clock for fast read/write (Prescaler /8 = 9 MHz)
        SPI1->CR1 &= ~SPI_CR1_SPE;
        SPI1->CR1 &= ~(SPI_CR1_BR_2 | SPI_CR1_BR_1 | SPI_CR1_BR_0);
        SPI1->CR1 |= SPI_CR1_BR_1; // Prescaler /8
        SPI1->CR1 |= SPI_CR1_SPE;
        return true;
    }

    sdCardReady = false;
    return false;
}

bool SD_IsAvailable(void) {
    return sdCardReady;
}

/**
 * @brief Read a single 512-byte block from SD Card
 */
bool SD_ReadBlock(uint32_t blockNum, uint8_t *buffer) {
    if (!sdCardReady || buffer == NULL) return false;

    uint32_t addr = isSDHC ? blockNum : (blockNum * 512);
    uint8_t res = SD_SendCommand(17, addr, 0xFF); // CMD17 = READ_SINGLE_BLOCK
    if (res != 0x00) {
        SD_Deselect();
        return false;
    }

    // Wait for start data token 0xFE
    uint32_t timeout = 2500;
    uint8_t token;
    do {
        token = SPI1_Transfer(0xFF);
        timeout--;
    } while ((token != 0xFE) && (timeout > 0));

    if (token != 0xFE) {
        SD_Deselect();
        return false;
    }

    // Read 512 data bytes
    for (int i = 0; i < 512; i++) {
        buffer[i] = SPI1_Transfer(0xFF);
    }

    // Read 2 CRC bytes
    SPI1_Transfer(0xFF);
    SPI1_Transfer(0xFF);

    SD_Deselect();
    return true;
}

/**
 * @brief Write a single 512-byte block to SD Card
 */
bool SD_WriteBlock(uint32_t blockNum, const uint8_t *buffer) {
    if (!sdCardReady || buffer == NULL) return false;

    uint32_t addr = isSDHC ? blockNum : (blockNum * 512);
    uint8_t res = SD_SendCommand(24, addr, 0xFF); // CMD24 = WRITE_SINGLE_BLOCK
    if (res != 0x00) {
        SD_Deselect();
        return false;
    }

    // Send dummy byte before data token
    SPI1_Transfer(0xFF);

    // Send Start Data Token 0xFE
    SPI1_Transfer(0xFE);

    // Send 512 data bytes
    for (int i = 0; i < 512; i++) {
        SPI1_Transfer(buffer[i]);
    }

    // Send 2 dummy CRC bytes
    SPI1_Transfer(0xFF);
    SPI1_Transfer(0xFF);

    // Read data response token
    uint8_t resp = SPI1_Transfer(0xFF);
    if ((resp & 0x1F) != 0x05) { // 0b00101 = Data accepted
        SD_Deselect();
        return false;
    }

    // Wait for card to finish programming (busy signal = LOW)
    uint32_t timeout = 50000;
    while ((SPI1_Transfer(0xFF) == 0x00) && (timeout > 0)) {
        timeout--;
    }

    SD_Deselect();
    return (timeout > 0);
}

/**
 * @brief Persistent Configuration Storage
 */
static int cfg_p1_h = 7, cfg_p1_m = 30;
static int cfg_p2_h = 19, cfg_p2_m = 0;
static bool configLoaded = false;

bool SD_LoadConfig(int *p1_hour, int *p1_min, int *p2_hour, int *p2_min) {
    if (!sdCardReady) return false;

    static uint8_t sectorBuf[512];
    if (SD_ReadBlock(SD_BLOCK_CONFIG, sectorBuf)) {
        if (strncmp((char*)sectorBuf, "PILLBOX_CFG_V1", 14) == 0) {
            int h1, m1, h2, m2;
            if (sscanf((char*)sectorBuf + 14, " P1=%d:%d P2=%d:%d", &h1, &m1, &h2, &m2) == 4) {
                if (h1 >= 0 && h1 < 24 && m1 >= 0 && m1 < 60 &&
                    h2 >= 0 && h2 < 24 && m2 >= 0 && m2 < 60) {
                    cfg_p1_h = h1; cfg_p1_m = m1;
                    cfg_p2_h = h2; cfg_p2_m = m2;
                    configLoaded = true;

                    *p1_hour = cfg_p1_h;
                    *p1_min  = cfg_p1_m;
                    *p2_hour = cfg_p2_h;
                    *p2_min  = cfg_p2_m;
                    return true;
                }
            }
        }
    }

    // If no valid config existed, save default config to SD card so it's initialized
    SD_SaveConfig(7, 30, 19, 0);

    *p1_hour = 7;
    *p1_min  = 30;
    *p2_hour = 19;
    *p2_min  = 0;
    return true;
}

bool SD_SaveConfig(int p1_hour, int p1_min, int p2_hour, int p2_min) {
    cfg_p1_h = p1_hour;
    cfg_p1_m = p1_min;
    cfg_p2_h = p2_hour;
    cfg_p2_m = p2_min;
    configLoaded = true;

    if (!sdCardReady) return false;

    static uint8_t sectorBuf[512];
    memset(sectorBuf, 0, sizeof(sectorBuf));
    snprintf((char*)sectorBuf, sizeof(sectorBuf), 
             "PILLBOX_CFG_V1 P1=%02d:%02d P2=%02d:%02d\r\n",
             p1_hour, p1_min, p2_hour, p2_min);

    return SD_WriteBlock(SD_BLOCK_CONFIG, sectorBuf);
}

/**
 * @brief Append Event to Log Sector on SD Card
 */
bool SD_LogEvent(SD_LogEvent_t event, uint16_t year, uint8_t month, uint8_t day, 
                 uint8_t hour, uint8_t minute, uint8_t second, const char *extraDetails) {
    if (!sdCardReady) return false;

    const char *evtStr = "EVENT";
    switch (event) {
        case SD_EVENT_DOSE_1_TAKEN:    evtStr = "PILL_1_TAKEN"; break;
        case SD_EVENT_DOSE_2_TAKEN:    evtStr = "PILL_2_TAKEN"; break;
        case SD_EVENT_WRONG_BUTTON:    evtStr = "WRONG_BUTTON"; break;
        case SD_EVENT_CLOCK_ADJUSTED:  evtStr = "CLOCK_ADJUST"; break;
        case SD_EVENT_CONFIG_UPDATED:  evtStr = "CONFIG_UPDATE"; break;
    }

    char logLine[80];
    snprintf(logLine, sizeof(logLine), 
             "%04d-%02d-%02d %02d:%02d:%02d,%s,%s\r\n",
             year, month, day, hour, minute, second, evtStr, extraDetails ? extraDetails : "-");

    static uint8_t logBuf[512];
    // Read current log sector
    if (!SD_ReadBlock(SD_BLOCK_LOG_START, logBuf)) {
        memset(logBuf, 0, sizeof(logBuf));
    }

    // Find end of current string
    size_t curLen = strnlen((char*)logBuf, sizeof(logBuf));
    if (curLen + strlen(logLine) < sizeof(logBuf) - 1) {
        memcpy(logBuf + curLen, logLine, strlen(logLine));
        logBuf[curLen + strlen(logLine)] = '\0';
    } else {
        // If block is full, rotate: shift content or start new block
        memset(logBuf, 0, sizeof(logBuf));
        memcpy(logBuf, logLine, strlen(logLine));
    }

    return SD_WriteBlock(SD_BLOCK_LOG_START, logBuf);
}

/* ==============================================================================
 * 3-DAY DOSAGE HISTORY IN-MEMORY MANAGEMENT
 * ============================================================================== */

static uint8_t getDaysInMonth(uint16_t year, uint8_t month) {
    if (month == 2) {
        bool isLeap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        return isLeap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        return 30;
    }
    return 31;
}

static void calculatePastDate(uint16_t curYear, uint8_t curMonth, uint8_t curDay, int daysBack,
                              uint16_t *outYear, uint8_t *outMonth, uint8_t *outDay) {
    int y = curYear;
    int m = curMonth;
    int d = curDay;

    for (int i = 0; i < daysBack; i++) {
        d--;
        if (d < 1) {
            m--;
            if (m < 1) {
                m = 12;
                y--;
            }
            d = getDaysInMonth(y, m);
        }
    }

    *outYear = (uint16_t)y;
    *outMonth = (uint8_t)m;
    *outDay = (uint8_t)d;
}

void DoseHistory_Init(DailyDoseLog_t history[3], uint16_t curYear, uint8_t curMonth, uint8_t curDay,
                      int p1_hour, int p1_min, int p2_hour, int p2_min, int curHour, int curMin) {
    for (int i = 0; i < 3; i++) {
        calculatePastDate(curYear, curMonth, curDay, i, &history[i].year, &history[i].month, &history[i].day);
        history[i].p1_status = DOSE_STATUS_EMPTY;
        history[i].p2_status = DOSE_STATUS_EMPTY;
        history[i].p1_hour = 0; history[i].p1_min = 0;
        history[i].p2_hour = 0; history[i].p2_min = 0;
    }

    // Set today's statuses based on current time
    history[0].p1_status = DOSE_STATUS_PENDING;
    history[0].p2_status = DOSE_STATUS_PENDING;

    // Past days (Day 1: Yesterday, Day 2: Day Before) show TAKEN as demo history if empty
    history[1].p1_status = DOSE_STATUS_TAKEN;
    history[1].p1_hour = p1_hour; history[1].p1_min = (p1_min + 2) % 60;
    history[1].p2_status = DOSE_STATUS_TAKEN;
    history[1].p2_hour = p2_hour; history[1].p2_min = (p2_min + 5) % 60;

    history[2].p1_status = DOSE_STATUS_TAKEN;
    history[2].p1_hour = p1_hour; history[2].p1_min = p1_min;
    history[2].p2_status = DOSE_STATUS_MISSED;

    DoseHistory_UpdateStatus(history, p1_hour, p1_min, p2_hour, p2_min, curHour, curMin);
}

void DoseHistory_RecordDose(DailyDoseLog_t history[3], int pillNum, uint8_t hour, uint8_t minute) {
    if (pillNum == 1) {
        history[0].p1_status = DOSE_STATUS_TAKEN;
        history[0].p1_hour = hour;
        history[0].p1_min = minute;
    } else if (pillNum == 2) {
        history[0].p2_status = DOSE_STATUS_TAKEN;
        history[0].p2_hour = hour;
        history[0].p2_min = minute;
    }
}

void DoseHistory_UpdateStatus(DailyDoseLog_t history[3], int p1_hour, int p1_min, int p2_hour, int p2_min,
                              int curHour, int curMin) {
    int nowMinutes = curHour * 60 + curMin;
    int p1Minutes = p1_hour * 60 + p1_min;
    int p2Minutes = p2_hour * 60 + p2_min;

    // For Pill 1 today: if not taken and time passed by > 60 mins, mark MISSED
    if (history[0].p1_status != DOSE_STATUS_TAKEN) {
        if (nowMinutes >= p1Minutes + 60) {
            history[0].p1_status = DOSE_STATUS_MISSED;
        } else {
            history[0].p1_status = DOSE_STATUS_PENDING;
        }
    }

    // For Pill 2 today: if not taken and time passed by > 60 mins, mark MISSED
    if (history[0].p2_status != DOSE_STATUS_TAKEN) {
        if (nowMinutes >= p2Minutes + 60) {
            history[0].p2_status = DOSE_STATUS_MISSED;
        } else {
            history[0].p2_status = DOSE_STATUS_PENDING;
        }
    }
}

void DoseHistory_CheckDayChange(DailyDoseLog_t history[3], uint16_t curYear, uint8_t curMonth, uint8_t curDay,
                                int p1_hour, int p1_min, int p2_hour, int p2_min) {
    if (history[0].day == curDay && history[0].month == curMonth && history[0].year == curYear) {
        return; // Same day
    }

    // Midnight rollover: shift history
    history[2] = history[1];
    history[1] = history[0];

    // Any pending dose on yesterday becomes missed
    if (history[1].p1_status == DOSE_STATUS_PENDING) history[1].p1_status = DOSE_STATUS_MISSED;
    if (history[1].p2_status == DOSE_STATUS_PENDING) history[1].p2_status = DOSE_STATUS_MISSED;

    // Reset today (Day 0)
    history[0].year = curYear;
    history[0].month = curMonth;
    history[0].day = curDay;
    history[0].p1_status = DOSE_STATUS_PENDING;
    history[0].p2_status = DOSE_STATUS_PENDING;
    history[0].p1_hour = 0; history[0].p1_min = 0;
    history[0].p2_hour = 0; history[0].p2_min = 0;
}
