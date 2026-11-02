# 2. State machine

`currentState` in `main.c` holds one of ten states (`SystemState_t` in `ui_screens.h`).

## Normal use

| State | What it shows | Leaves when |
|---|---|---|
| `NORMAL_IDLE` | clock, both dose times, "READY & MONITORING" | B3 pressed (to settings), or a dose time arrives |
| `REMINDER_DOSE_1` | blinking "MEDICINE! TAKE PILL 1" | B1 pressed (correct) |
| `REMINDER_DOSE_2` | blinking "MEDICINE! TAKE PILL 2" | B2 pressed (correct) |
| `DOSE_CONFIRMED` | "DOSE TAKEN, Recorded!" | after 2 seconds, back to idle |

### Alarm trigger
In `NORMAL_IDLE`, each loop compares the clock with each dose time. When hours and minutes both match and `lastTriggeredDay` differs from today's date, the alarm starts and `lastTriggeredDay` is set. This makes each alarm fire once per day.

### During an alarm
- Every 500 ms (150 ms after a wrong press) the visual state flips, and `Alarms_Update` toggles the LED and buzzer to match.
- **Correct button:** stops the LED and buzzer, stores the dose in the history, logs `PILL_x_TAKEN` to the SD card, goes to `DOSE_CONFIRMED`.
- **Wrong button** (once): the screen shows "WRONG PILL!", blinking speeds up to 150 ms, and `WRONG_BUTTON` is logged.
- There is no time-out: the alarm continues until the correct button is pressed.

## Settings

| State | Buttons |
|---|---|
| `SETUP_MENU` | B1 next item, B2 select, B3 exit. Items: 1 Pill 1 time, 2 Pill 2 time, 3 Adjust clock, 4 View logs |
| `SETUP_EDIT_HOUR` | B1 +1 hour (wraps at 24), B2 go to minutes |
| `SETUP_EDIT_MINUTE` | B1 +1 minute (wraps at 60), B2 go to confirm |
| `SETUP_CONFIRM` | B1 toggles YES/CANCEL, B2 accepts |
| `SETUP_FEEDBACK` | "TIME SAVED!" or "DISCARDED" for 1.5 s, then idle |
| `VIEW_LOGS` | B1 next day (today, -1, -2), B3 back |

The edited value sits in `tempHour` / `tempMinute` and only replaces the real one on YES.
- **Pill time saved:** the new time goes to `dose1` or `dose2`, `lastTriggeredDay` resets to -1 (so it can fire today), the config is written to the SD card, and a `CONFIG_UPDATE` line is logged.
- **Clock adjusted:** hours and minutes are set, seconds go to 0, `DS1307_SetTime` writes the RTC, and `CLOCK_ADJUST` is logged.

## Notes on behaviour
- Alarms only start from `NORMAL_IDLE`. A dose time that arrives while you are in the settings menu does not start an alarm.
- While the cursor blinks in an edit state, the screen redraws every 400 ms.
