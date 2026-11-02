/**
  ******************************************************************************
  * @file    oled_test.h
  * @brief   Comprehensive SSD1306 128x64 I2C OLED Diagnostic & Test Suite
  *          Target: STM32F103C8T6 (Blue Pill) + SSD1306 (0x3C / 0x3D)
  ******************************************************************************
  */

#ifndef OLED_TEST_H
#define OLED_TEST_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Display Configuration --- */
#define OLED_TEST_WIDTH         128
#define OLED_TEST_HEIGHT        64
#define OLED_TEST_BUFFER_SIZE   (OLED_TEST_WIDTH * OLED_TEST_HEIGHT / 8) // 1024 bytes

/* Default 8-bit shifted I2C address for HAL (0x3C -> 0x78, 0x3D -> 0x7A) */
#define OLED_I2C_ADDR_PRIMARY   (0x3C << 1)
#define OLED_I2C_ADDR_SECONDARY (0x3D << 1)

/* --- Diagnostic Results Struct --- */
typedef struct {
    bool     deviceDetected;      /* True if OLED acknowledged I2C ping */
    uint16_t detectedAddress;     /* Shifted 8-bit I2C address found */
    uint32_t frameTimeMs;         /* Measured average time to push 1 frame */
} OLED_TestReport_t;

/* --- High-Level Test Runner --- */
/**
 * @brief  Executes all diagnostic tests sequentially with visual commentary.
 * @param  hi2c: Pointer to initialized I2C_HandleTypeDef (e.g., &hi2c1)
 * @return OLED_TestReport_t with scan and timing results
 */
OLED_TestReport_t OLED_Test_RunAll(I2C_HandleTypeDef *hi2c);

/* --- Modular Individual Tests --- */
bool OLED_Test_I2C_Ping(I2C_HandleTypeDef *hi2c, uint16_t *outAddress);
bool OLED_Test_Init_Clean(I2C_HandleTypeDef *hi2c, uint16_t addr);
void OLED_Test_Clear(uint8_t color);
void OLED_Test_UpdateScreen(I2C_HandleTypeDef *hi2c, uint16_t addr);

/* Graphic Primitives for Testing */
void OLED_Test_DrawPixel(int x, int y, uint8_t color);
void OLED_Test_DrawLine(int x0, int y0, int x1, int y1, uint8_t color);
void OLED_Test_DrawRect(int x, int y, int w, int h, uint8_t color);
void OLED_Test_FillRect(int x, int y, int w, int h, uint8_t color);
void OLED_Test_DrawCircle(int x0, int y0, int r, uint8_t color);
void OLED_Test_DrawChar(int x, int y, char c, uint8_t size, uint8_t color, uint8_t bg);
void OLED_Test_DrawString(int x, int y, const char *str, uint8_t size, uint8_t color, uint8_t bg);

/* Pattern Routines */
void OLED_Test_ShowSplash(I2C_HandleTypeDef *hi2c, uint16_t addr, const char *title);
void OLED_Test_BordersAndGeometry(I2C_HandleTypeDef *hi2c, uint16_t addr);
void OLED_Test_TextAndFonts(I2C_HandleTypeDef *hi2c, uint16_t addr);
void OLED_Test_PixelFillAndInvert(I2C_HandleTypeDef *hi2c, uint16_t addr);
void OLED_Test_Animation(I2C_HandleTypeDef *hi2c, uint16_t addr, uint16_t durationMs);

#ifdef __cplusplus
}
#endif

#endif /* OLED_TEST_H */

