/**
  ******************************************************************************
  * @file    buttons.h
  * @brief   User Pushbuttons & Software Debouncing Driver
  *          Target: STM32F103C8T6 (Blue Pill)
  ******************************************************************************
  */

#ifndef INC_BUTTONS_H_
#define INC_BUTTONS_H_

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pin Assignments (Active LOW with internal pull-up) */
#define BUTTON_PORT       GPIOB
#define BUTTON_1_PIN      GPIO_PIN_12 // PB12: Pill 1 Dose confirm / Menu +
#define BUTTON_2_PIN      GPIO_PIN_14 // PB14: Pill 2 Dose confirm / Menu select
#define BUTTON_3_PIN      GPIO_PIN_15 // PB15: Open Setup Menu / Exit

#define DEBOUNCE_TIME_MS  200

/* Function Prototypes */
void Buttons_Init(void);
void Buttons_Scan(uint32_t nowMs, bool *btn1Pressed, bool *btn2Pressed, bool *btn3Pressed);

#ifdef __cplusplus
}
#endif

#endif /* INC_BUTTONS_H_ */

