# Issue 3: Build errors with the test file

**Status:** fixed.

## Errors seen
1. `void value not ignored as it ought to be` at `bool oledPresent = ssd1306_Init(&hi2c1);`
2. `'SD_DebugStage' undeclared` (and `SD_DebugResp`, `SD_DebugSpiTimeouts`) in `testSD`.

## Cause
The files in `D:\WORK\mc\MC` and the files the STM32CubeIDE project built were not the same version. The IDE used:
- an older `ssd1306.h` (where `ssd1306_Init` returns `void`)
- an older `sd_card.h` (without the debug variables)

The IDE build line shows `-I../Core/Inc`, so it compiles whatever is in the IDE project's own `Core/Inc`. Edits made in the repository folder only reach the IDE project if the files are copied or the IDE project is that same folder.

## Fix
- `test/main_full_test.c` no longer uses the return value of `ssd1306_Init`; it probes `0x3C` and `0x3D` itself with `HAL_I2C_IsDeviceReady`.
- The SD debug variables are declared in the test file with `__attribute__((weak))`, and the test checks the address before reading them. The test then builds with either the old or the new `sd_card.c/.h`.

## Lesson
When something compiles on disk but not in the IDE, compare the IDE project's copies of the files with the repository's before debugging the code. Files that must match:
`Core/Src/ssd1306.c`, `Core/Inc/ssd1306.h`, `Core/Src/sd_card.c`, `Core/Inc/sd_card.h`, `Core/Src/ui_screens.c`, `Core/Src/main.c`.

Note: to run the test, `test/main_full_test.c` is pasted over `Core/Src/main.c`. Do not commit that as `main.c`; restore the real `main.c` first.
