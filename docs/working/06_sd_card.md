# 6. SD card

**Status: not working on the current hardware.** See [issues/02](../issues/02_sd_card_not_detected.md). This page describes how it is meant to work.

## Pins
SPI1: CS PA4 (plain GPIO output), SCK PA5, MOSI PA7 (alternate-function push-pull), MISO PA6 (input with pull-up).

## No filesystem
The driver talks to the card in **SPI mode** and reads/writes raw 512-byte sectors. There is no FAT; a PC will not see files.

| Sector | Use |
|---|---|
| 1000 | config |
| 1001 | event log (`SD_BLOCK_LOG_COUNT` is 10, but only 1001 is used) |
| 1010 | scratch sector used only by the test firmware |

## Low level
`SPI1_Transfer(byte)` sends one byte and returns the byte that arrived at the same time. It waits for TXE (can send) then RXNE (received); each wait now gives up after 100000 loops.

`SD_SendCommand(cmd, arg, crc)` pulls CS low and sends a 6-byte packet: `0x40|cmd`, the 4-byte argument, the CRC byte. It then clocks `0xFF` until the card answers with a byte whose top bit is 0 (up to 200 tries).

## Start-up sequence (`SD_Init`)
1. Set up the pins and SPI1 at about 281 kHz (prescaler 256). Cards must start slowly.
2. With CS high, send 10 bytes (80 clocks) so the card switches to SPI mode.
3. **CMD0** (reset), CRC `0x95`. The card must answer `0x01` ("idle").
4. **CMD8** with `0x1AA`: asks if the card accepts 2.7 to 3.6 V. If it answers, 4 more bytes are read.
5. Repeat **CMD55 + ACMD41** (with the high-capacity bit) until the card answers `0x00` ("ready"), up to 500 tries with 2 ms between.
6. **CMD58** reads the OCR to see whether the card uses block addressing (SDHC) or byte addressing.
7. Speed SPI up to about 9 MHz (prescaler 8). Return true.

If CMD0 does not return `0x01`, or ACMD41 never returns `0x00`, `SD_Init` returns false.

`SD_DebugStage`, `SD_DebugResp` and `SD_DebugSpiTimeouts` record how far it got.

## Reading and writing a sector
- **Read:** CMD17 with the block number (or byte address `block * 512` on non-SDHC cards), wait for token `0xFE`, read 512 bytes and 2 CRC bytes.
- **Write:** CMD24, send `0xFE`, 512 bytes, 2 dummy CRC bytes, check the card's reply is "data accepted" (`xxx00101`), then wait while the card holds MISO low (busy) up to a limit.

## Config (`SD_LoadConfig` / `SD_SaveConfig`)
Sector 1000 holds one line of text:
```
PILLBOX_CFG_V1 P1=07:30 P2=19:00
```
On load: read the sector, check the `PILLBOX_CFG_V1` prefix, parse the four numbers with `sscanf`, and accept them only if hours are 0..23 and minutes 0..59. If the sector is invalid, it writes the defaults (07:30 and 19:00) and returns those. If there is no card, the program uses the defaults built into `main.c`.

## Event log (`SD_LogEvent`)
Builds a line like:
```
2026-11-03 07:30:12,PILL_1_TAKEN,Confirmed Button 1
```
Event names: `PILL_1_TAKEN`, `PILL_2_TAKEN`, `WRONG_BUTTON`, `CLOCK_ADJUST`, `CONFIG_UPDATE`.

It reads sector 1001, appends the line to the text already there, and writes the sector back. When the sector would overflow (512 bytes, about 6 to 8 lines), it **erases it and starts again**, so only the most recent few events are kept.

Each log call is a full read plus a full write, which blocks the main loop for the duration.

## Other notes
- The SD code also holds the 3-day history functions ([07_dose_history.md](07_dose_history.md)) because they share the same header.
- With no card, `SD_LogEvent` and `SD_SaveConfig` return false and the rest of the program carries on.
- Possible causes of the current failure are listed in the issues page.
