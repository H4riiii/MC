/**
  ******************************************************************************
  * @file    alarms.h
  * @brief   Audio-Visual Alarm Indicators (LEDs & Piezo Buzzer)
  *          Target: STM32F103C8T6 (Blue Pill)
  ******************************************************************************
  */

#ifndef INC_ALARMS_H_
#define INC_ALARMS_H_

#include "stm32f1xx_hal.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hardware Feature Flags */
#define ENABLE_LEDS       1   // 1 = Enable LEDs on PB1 (Pill 1) and PB10 (Pill 2)
#define ENABLE_BUZZER     1   // 1 = Enable Buzzer on PB0

/* Pin Assignments */
#define LED_PORT          GPIOB
#define LED_1_PIN         GPIO_PIN_1   // PB1: Pill 1 LED
#define LED_2_PIN         GPIO_PIN_10  // PB10: Pill 2 LED

#define BUZZER_PORT       GPIOB
#define BUZZER_PIN        GPIO_PIN_0   // PB0: Buzzer

/* Alarm Functions */
void Alarms_Init(void);
void Alarms_Update(bool state, bool isPill1, bool isFastBlink);
void Alarms_Stop(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_ALARMS_H_ */

