# 5. Buttons, LEDs and buzzer

## Buttons (`buttons.c`)
Three push buttons on PB12, PB14, PB15. Each connects its pin to ground when pressed, and the pin has the internal pull-up on, so:
- not pressed reads **high**,
- pressed reads **low**.

`Buttons_Scan(now, &b1, &b2, &b3)` is called every loop and sets each flag true for **one call only**, when:
1. the pin was high last time and is low now (a falling edge, i.e. a new press), and
2. more than 200 ms (`DEBOUNCE_TIME_MS`) have passed since the last accepted press of that button.

Effects:
- Holding a button gives one event, not repeats.
- Contact bounce right after a press is ignored.
- Two presses closer than 200 ms apart count as one.

## LEDs and buzzer (`alarms.c`)

| Output | Pin |
|---|---|
| LED 1 (pill 1) | PB1 |
| LED 2 (pill 2) | PB10 |
| Buzzer | PB0 |

`ENABLE_LEDS` and `ENABLE_BUZZER` in `alarms.h` turn each on or off at compile time.

- **`Alarms_Init`** configures the pins as outputs and sets them low.
- **`Alarms_Update(state, isPill1, isFastBlink)`** drives the LED of the active pill to `state` and the other LED off; the buzzer follows `state` too. The blink pattern comes from `main.c` calling it every 500 ms (or 150 ms after a wrong press) with the flipped state. `isFastBlink` is not used in the function.
- **`Alarms_Stop`** switches all three off.

The buzzer is driven as a plain on/off output, so it works with an active buzzer (one that makes its own tone). A passive buzzer would only click.

## Diagnostic use of LED 1
If the OLED does not answer at startup, `main.c` leaves LED 1 switched on. `Error_Handler` blinks it quickly if I2C fails to initialise.
