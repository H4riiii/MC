/**
  ******************************************************************************
  * @file    alarms.c
  * @brief   Audio-Visual Alarm Indicators (LEDs & Piezo Buzzer) implementation
  *          Target: STM32F103C8T6 (Blue Pill)
  ******************************************************************************
  */

#include "alarms.h"

void Alarms_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

#if ENABLE_LEDS
    GPIO_InitStruct.Pin = LED_1_PIN | LED_2_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LED_PORT, LED_1_PIN | LED_2_PIN, GPIO_PIN_RESET);
#endif

#if ENABLE_BUZZER
    GPIO_InitStruct.Pin = BUZZER_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
#endif
}

void Alarms_Update(bool state, bool isPill1, bool isFastBlink) {
#if ENABLE_LEDS
    if (isPill1) {
        HAL_GPIO_WritePin(LED_PORT, LED_1_PIN, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_PORT, LED_2_PIN, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(LED_PORT, LED_2_PIN, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED_PORT, LED_1_PIN, GPIO_PIN_RESET);
    }
#else
    (void)isPill1;
#endif

#if ENABLE_BUZZER
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    (void)isFastBlink;
#endif
    (void)state;
}

void Alarms_Stop(void) {
#if ENABLE_LEDS
    HAL_GPIO_WritePin(LED_PORT, LED_1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_PORT, LED_2_PIN, GPIO_PIN_RESET);
#endif

#if ENABLE_BUZZER
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_RESET);
#endif
}

