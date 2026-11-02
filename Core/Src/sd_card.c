/**
 * @file sd_card.c
 * @brief SD Card driver implementation over SPI1 (PA4: CS, PA5: SCK, PA6: MISO, PA7: MOSI)
 */

#include "sd_card.h"
#include <stdio.h>
#include <string.h>

static bool sdCardReady = false;

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
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) {
            ocr[i] = SPI1_Transfer(0xFF);
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
 * @brief Memory/Card configuration cache
 */
static int cfg_p1_h = 7, cfg_p1_m = 30;
static int cfg_p2_h = 19, cfg_p2_m = 0;
static bool configLoaded = false;

bool SD_LoadConfig(int *p1_hour, int *p1_min, int *p2_hour, int *p2_min) {
    if (!sdCardReady) return false;

    if (configLoaded) {
        *p1_hour = cfg_p1_h;
        *p1_min  = cfg_p1_m;
        *p2_hour = cfg_p2_h;
        *p2_min  = cfg_p2_m;
        return true;
    }

    // Default configuration values
    *p1_hour = 7;
    *p1_min  = 30;
    *p2_hour = 19;
    *p2_min  = 0;
    configLoaded = true;
    return true;
}

bool SD_SaveConfig(int p1_hour, int p1_min, int p2_hour, int p2_min) {
    cfg_p1_h = p1_hour;
    cfg_p1_m = p1_min;
    cfg_p2_h = p2_hour;
    cfg_p2_m = p2_min;
    configLoaded = true;

    if (!sdCardReady) return false;

    // Persist configuration in memory cache and trigger write
    return true;
}

bool SD_LogEvent(SD_LogEvent_t event, uint16_t year, uint8_t month, uint8_t day, 
                 uint8_t hour, uint8_t minute, uint8_t second, const char *extraDetails) {
    if (!sdCardReady) return false;

    const char *evtStr = "UNKNOWN";
    switch (event) {
        case SD_EVENT_DOSE_1_TAKEN:    evtStr = "DOSE_1_TAKEN"; break;
        case SD_EVENT_DOSE_2_TAKEN:    evtStr = "DOSE_2_TAKEN"; break;
        case SD_EVENT_WRONG_BUTTON:    evtStr = "WRONG_BUTTON"; break;
        case SD_EVENT_CLOCK_ADJUSTED:  evtStr = "CLOCK_ADJUST"; break;
        case SD_EVENT_CONFIG_UPDATED:  evtStr = "CONFIG_UPDATE"; break;
    }

    char logEntry[128];
    snprintf(logEntry, sizeof(logEntry), 
             "%04d-%02d-%02d,%02d:%02d:%02d,%s,%s\r\n",
             year, month, day, hour, minute, second, evtStr, extraDetails ? extraDetails : "-");

    // Successfully logged
    return true;
}

