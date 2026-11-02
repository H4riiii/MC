/**
  ******************************************************************************
  * @file    ssd1306.h
  * @brief   SSD1306 128x64 OLED display pure C driver (I2C)
  *          Target: STM32F103C8T6 (Blue Pill)
  ******************************************************************************
  */

#ifndef INC_SSD1306_H_
#define INC_SSD1306_H_

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Display geometry */
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     64
#define SSD1306_I2C_ADDR  (0x3C << 1) // 0x78: 8-bit shifted I2C address for STM32 HAL
#define SSD1306_ALT_ADDR  (0x3D << 1) // 0x7A: Alternative address

/* Core Driver Functions */
void ssd1306_Init(I2C_HandleTypeDef *hi2c);
void ssd1306_Clear(uint8_t color);   // fills the current 128-byte page only (no frame buffer)
void ssd1306_BeginPage(uint8_t page); // start rendering page 0..7 (clears it)
void ssd1306_EndPage(void);           // stream the rendered page to the OLED

/* A frame is drawn by running the same drawing code once per page:
 *   for (p = 0; p < 8; p++) { ssd1306_BeginPage(p); <draw everything>; ssd1306_EndPage(); }
 * Drawing calls only affect the page being rendered. */

/* Graphics Primitives */
void ssd1306_DrawPixel(int x, int y, uint8_t color);
void ssd1306_DrawLine(int x0, int y0, int x1, int y1, uint8_t color);
void ssd1306_DrawRect(int x, int y, int w, int h, uint8_t color);
void ssd1306_FillRect(int x, int y, int w, int h, uint8_t color);
void ssd1306_DrawChar(int x, int y, char c, uint8_t size, uint8_t color, uint8_t bg);
void ssd1306_DrawString(int x, int y, const char *str, uint8_t size, uint8_t color, uint8_t bg);

#ifdef __cplusplus
}
#endif

#endif /* INC_SSD1306_H_ */

