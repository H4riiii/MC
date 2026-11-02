# 7. Dose history (3 days)

An array of three `DailyDoseLog_t` lives in `main.c`: index 0 = today, 1 = yesterday, 2 = the day before. Each entry has the date and, for each pill, a status and the time it was taken.

## Statuses
`DOSE_STATUS_EMPTY`, `PENDING` (not yet due or still within the hour), `TAKEN`, `MISSED`.

## Functions (in `sd_card.c`)
- **`DoseHistory_Init`** fills in the three dates by counting back from today (handling month lengths and leap years), sets today to PENDING, and then calls `UpdateStatus`.
- **`DoseHistory_RecordDose(pill, hour, minute)`** marks today's pill TAKEN with the time. Called when you press the correct button.
- **`DoseHistory_UpdateStatus`** runs every 500 ms. For each of today's pills that is not TAKEN: if the current time is **at least 60 minutes after** the dose time it becomes MISSED, otherwise PENDING.
- **`DoseHistory_CheckDayChange`** runs every 500 ms. When today's date differs from `history[0]`, it shifts the entries (1 to 2, 0 to 1), turns yesterday's PENDING entries into MISSED, and starts a fresh today.

## Shown on screen
`VIEW_LOGS` shows, for the chosen day, the date and for each pill the scheduled time and `OK hh:mm`, `MISSED` or `PENDING`.

## Limits
- The history lives only in RAM and is lost on reset or power-off. It is not read back from the SD card.
- At start-up, `DoseHistory_Init` fills yesterday and the day before with **made-up** entries (invented TAKEN/MISSED values), so the first days shown are not real records.
- A pill is marked MISSED 60 minutes after its time even though the alarm keeps going until you press the button. Pressing it afterwards still marks it TAKEN.
- Because it compares times only (not dates), MISSED status can be wrong right after changing a dose time or the clock.
- The footer text of the log screen says "SD: SAVED [SEC 1001]" whenever a card was found; it is a fixed label.
