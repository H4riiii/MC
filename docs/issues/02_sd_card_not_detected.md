# Issue 2: SD card not detected

**Status:** open. The rest of the system works (OLED, RTC, buttons, LEDs, buzzer); the SD card is not detected.

## Symptoms
- In the full hardware test (`test/main_full_test.c`) the screen stuck at "SD CARD - initialising..." with the original `sd_card.c`.
- With the updated driver, SD init fails and the screen shows a diagnostic readout instead of hanging.
- In the main firmware, the splash screen shows `SD: NONE` and the `SD` marker is missing from the header.

## Why it hung (fixed in code)
The original `SPI1_Transfer()` in `Core/Src/sd_card.c` waited forever:
```c
while (!(SPI1->SR & SPI_SR_TXE));
while (!(SPI1->SR & SPI_SR_RXNE));
```
If SPI1 never produced a byte, the firmware blocked there. Both waits now have a limit (100000 loops) and return `0xFF` on timeout.

## Diagnostics added
`sd_card.c` now exposes:

| Variable | Meaning |
|---|---|
| `SD_DebugStage` | 1 low-level init, 2 SPI ready, 3 CMD0 sent, 4 CMD8 sent, 5 ACMD41 loop, 6 card ready |
| `SD_DebugResp` | last response byte (CMD0 should return `01`, ready state is `00`) |
| `SD_DebugSpiTimeouts` | number of SPI waits that timed out |

On failure the test screen shows `stage N resp XX` and `spi timeouts N`. **These numbers have not been collected yet.** They are the next step.

## How to read the numbers

| Screen shows | Likely cause |
|---|---|
| `resp FF`, timeouts 0 | Card not answering: wiring, power, card not seated |
| timeouts above 0 | SPI1 not running: pin or clock problem |
| stage 3, resp `01` but fails later | Card answered but never finished start-up (ACMD41): card type or power |
| "update sd_card.c/.h" | The IDE project still has the old SD driver (see Issue 3) |

## What might be wrong (nothing below is confirmed)

### Wiring and power (most likely)
- **MOSI/MISO swapped.** Required: CS PA4, SCK PA5, MISO PA6, MOSI PA7.
- **Module voltage.** Most microSD breakout boards have a regulator and need 5 V on VCC; some bare ones need 3.3 V. A wrong supply gives no response.
- **Loose jumpers**, especially CS (PA4) and MISO.
- **Missing common GND.**
- **Card:** not fully seated, not FAT32, larger than 32 GB, or a worn card. Try another card.
- **Level shifting:** a module that wants 5 V logic may not read the STM32's 3.3 V signals reliably, even though the module's regulator is fine.

### Possible weaknesses in the driver (`Core/Src/sd_card.c`, unconfirmed)
- **CMD0 is tried only once.** Some cards need several attempts; a single missed reply ends init as "no card".
- **No dummy clocks check before CMD0.** The driver sends 10 bytes (80 clocks) with CS high; that is enough in theory, but a very slow-powering card might need a delay before it.
- **CMD8 reply is ignored.** The result of CMD8 (`0x1AA` echo) is read but not checked, so an old (v1) card or a bad reply is not told apart from a good one.
- **ACMD41 always sets the HCS bit (`0x40000000`)** even for cards that did not accept CMD8. This is allowed for v2 cards but can keep v1 cards from becoming ready.
- **CRC bytes:** only CMD0 (`0x95`) and CMD8 (`0x87`) use valid CRCs, which is correct, but later commands send a fixed value (`0x65`, `0x77`, `0xFF`) which relies on the card ignoring CRC in SPI mode (the default).
- **MISO is a plain input with internal pull-up.** If MISO floats or is held weakly, every response reads `FF`.
- **Clock after init:** SPI is sped up to /8 (9 MHz). Some modules with long wires are unreliable at that speed. Not the cause of detection failing (init runs at about 281 kHz), but could cause read/write errors later.
- **The SPI peripheral is configured by direct register writes** rather than HAL. It has not been checked against the F103 reference manual for every bit (for example the AFIO clock is not enabled, which is normally fine for the default SPI1 pins).

### Raw sector storage (design concern, not the cause of this fault)
The firmware writes raw sectors with no filesystem: config at block `1000`, log at `1001`-`1010`. This can **overwrite data on a card that holds a filesystem**, and a PC will not see a readable file. The test uses block `1010` as scratch and restores it. Use a spare card.

## Next steps
1. Copy the current `Core/Src/sd_card.c` and `Core/Inc/sd_card.h` into the IDE project.
2. Run `test/main_full_test.c` with `RUN_SD_TEST` set to `1`.
3. Note `stage`, `resp` and `spi timeouts` from the failure screen.
4. Check wiring and supply voltage as above; try another card.
5. If the numbers still do not explain it, make CMD0 retry several times, check the CMD8 reply, and send the HCS bit only when CMD8 succeeded.
