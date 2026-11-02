/**
  ******************************************************************************
  * @file    buttons.c
  * @brief   User Pushbuttons & Software Debouncing Driver implementation
  *          Target: STM32F103C8T6 (Blue Pill)
  ******************************************************************************
  */

#include "buttons.h"

static GPIO_PinState lastButton1State = GPIO_PIN_SET;
static GPIO_PinState lastButton2State = GPIO_PIN_SET;
static GPIO_PinState lastButton3State = GPIO_PIN_SET;

static uint32_t lastButton1Press = 0;
static uint32_t lastButton2Press = 0;
static uint32_t lastButton3Press = 0;

void Buttons_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Configure PB12, PB14, PB15 as Input with internal pull-up
    GPIO_InitStruct.Pin = BUTTON_1_PIN | BUTTON_2_PIN | BUTTON_3_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BUTTON_PORT, &GPIO_InitStruct);
}

void Buttons_Scan(uint32_t nowMs, bool *btn1Pressed, bool *btn2Pressed, bool *btn3Pressed) {
    *btn1Pressed = false;
    *btn2Pressed = false;
    *btn3Pressed = false;

    GPIO_PinState b1 = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_1_PIN);
    GPIO_PinState b2 = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_2_PIN);
    GPIO_PinState b3 = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_3_PIN);

    // Falling edge detection with debounce time window
    if (lastButton1State == GPIO_PIN_SET && b1 == GPIO_PIN_RESET) {
        if (nowMs - lastButton1Press > DEBOUNCE_TIME_MS) {
            lastButton1Press = nowMs;
            *btn1Pressed = true;
        }
    }

    if (lastButton2State == GPIO_PIN_SET && b2 == GPIO_PIN_RESET) {
        if (nowMs - lastButton2Press > DEBOUNCE_TIME_MS) {
            lastButton2Press = nowMs;
            *btn2Pressed = true;
        }
    }

    if (lastButton3State == GPIO_PIN_SET && b3 == GPIO_PIN_RESET) {
        if (nowMs - lastButton3Press > DEBOUNCE_TIME_MS) {
            lastButton3Press = nowMs;
            *btn3Pressed = true;
        }
    }

    lastButton1State = b1;
    lastButton2State = b2;
    lastButton3State = b3;
}

