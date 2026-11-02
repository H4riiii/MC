# DS1307 Real-Time Clock & I2C Subsystem

## 1. Overview of DS1307 RTC
The **DS1307** is a low-power, full binary-coded decimal (BCD) clock/calendar real-time clock IC manufactured by Analog Devices (formerly Maxim Integrated). It tracks seconds, minutes, hours, day of the week, date of the month, month, and year with leap-year compensation valid up to the year 2100.

---

## 2. Technical Specifications

| Parameter | Specification |
| :--- | :--- |
| **Interface** | I2C Serial Bus (Standard 100 kHz) |
| **I2C Device Address** | 7-bit: `0x68` (STM32 HAL 8-bit shifted: `0xD0` Write, `0xD1` Read) |
| **Operating Voltage ($V_{CC}$)** | 4.5 V to 5.5 V (Nominal: 5.0 V) |
| **Backup Battery ($V_{BAT}$)** | 2.0 V to 3.5 V (Standard 3.0 V CR2032 Lithium Coin Cell) |
| **Battery Backup Current** | Less than 500 nA (enabling 10+ years of operation on a single coin cell) |
| **Oscillator Frequency** | 32.768 kHz Quartz Crystal connected across `X1` and `X2` |
| **Internal RAM** | 56 bytes of non-volatile battery-backed general purpose SRAM (`0x08` to `0x3F`) |

---

## 3. Register Map & Binary-Coded Decimal (BCD)

The DS1307 timekeeper registers occupy addresses `0x00` through `0x07`:

| Address | Bit 7 | Bit 6 | Bit 5 | Bit 4 | Bit 3 | Bit 2 | Bit 1 | Bit 0 | Function | Range |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- | :---: |
| **`0x00`** | **CH** | 10 Seconds | Seconds | Seconds | Seconds | Seconds | Seconds | Seconds | Seconds & Clock Halt | 00–59 |
| **`0x01`** | 0 | 10 Minutes | Minutes | Minutes | Minutes | Minutes | Minutes | Minutes | Minutes | 00–59 |
| **`0x02`** | 0 | **12/24** | 10 Hr/PM | 10 Hr | Hours | Hours | Hours | Hours | Hours | 00–23 (or 1–12) |
| **`0x03`** | 0 | 0 | 0 | 0 | 0 | Day | Day | Day | Day of Week | 1–7 |
| **`0x04`** | 0 | 0 | 10 Date | Date | Date | Date | Date | Date | Date of Month | 01–31 |
| **`0x05`** | 0 | 0 | 0 | 10 Month | Month | Month | Month | Month | Month | 01–12 |
| **`0x06`** | 10 Year | Year | Year | Year | Year | Year | Year | Year | Year | 00–99 |
| **`0x07`** | OUT | 0 | 0 | SQWE | 0 | 0 | RS1 | RS0 | Control Register | - |

---

## 4. Key Implementation Details in Firmware

### 4.1 BCD to Decimal Conversion
The DS1307 represents digits using separate 4-bit nibbles for tens and units.
* E.g. Minute 45 is stored as binary `0100 0101b` = `0x45`.
* Our driver provides rapid bit-shift conversion:
```c
#define BCD_TO_DEC(val) (((val) >> 4) * 10 + ((val) & 0x0F))
#define DEC_TO_BCD(val) ((((val) / 10) << 4) | ((val) % 10))
```

### 4.2 Handling the Clock Halt (`CH`) Flag
* **Bit 7 of Register `0x00`** is the **Clock Halt (CH)** bit.
* When power is first applied with a fresh battery, the oscillator is stopped and `CH = 1`.
* In `DS1307_Init()`, the firmware reads register `0x00`, verifies if `CH == 1`, and if so, writes `0` back to bit 7 to start the 32.768 kHz oscillator.

### 4.3 Burst Reading Over I2C
Rather than requesting each register individually, `DS1307_GetTime()` sends the starting register address `0x00` and reads all 7 bytes continuously in one transaction:
```c
HAL_I2C_Mem_Read(hi2c, DS1307_I2C_ADDR, DS1307_REG_SECONDS, 1, buf, 7, 100);
```
The internal register pointer of the DS1307 automatically auto-increments after each byte read.

### 4.4 Hardware Practicalities & Diagnostic Bus Scanner
* **Power Supply Requirement ($5\text{V}$ vs $3.3\text{V}$)**: The DS1307 has an internal power-fail comparator that trips if $V_{CC} < 1.25 \times V_{BAT} (\approx 3.75\text{ V} - 4.0\text{ V})$. Supplying 3.3V locks the chip into battery-backup write-protect mode and disables the I2C interface. Therefore, $V_{CC}$ must connect to the **$5\text{V}$ rail** of the Blue Pill.
* **$V_{BAT}$ Floating Hazard**: If no coin cell is inserted, the $V_{BAT}$ pin will float, which can prevent the I2C interface from acknowledging. To operate without a battery, the `BAT` header pin must be tied to `GND`.
* **Tiny RTC Trickle-Charge Circuit Warning**: Standard "Tiny RTC" breakout modules include diode `D1` and resistor `R5` forming a crude charging path meant for rechargeable **LIR2032** cells. If using ordinary disposable **CR2032** lithium coin cells, desoldering diode `D1` is required to prevent overcharging.
* **On-Screen I2C Diagnostic Scanner**: At boot, the firmware scans addresses `0x01` through `0x7F` and renders detected slaves on the OLED display (`0x3C` for OLED, `0x68` for DS1307, `0x50` for companion 24C32 EEPROM), immediately confirming physical connectivity.
* **Non-Blocking Software Clock Fallback**: If the DS1307 is disconnected or unpowered, the firmware falls back to `HAL_GetTick()` elapsed time interpolation, keeping the clock ticking with an alert icon (`!`) without halting medication reminders.

---

## 5. Potential Evaluator Questions & Answers

* **Q: Why does the DS1307 require a 32.768 kHz crystal?**
  * *A: $32,768 = 2^{15}$. Passing this frequency through a 15-stage binary divider (flip-flop chain) yields an exact $1.000\,\text{Hz}$ (1-second) tick with minimal low-power CMOS logic.*
* **Q: Can we power the DS1307 with 3.3V?**
  * *A: No. The DS1307 requires $4.5\text{ V} \text{ to } 5.5\text{ V}$. At 3.3V, its internal comparator detects an undervoltage condition and inhibits the I2C bus to protect registers.*
* **Q: What is the purpose of the CR2032 coin cell?**
  * *A: When main power ($V_{CC}$) drops below $1.25 \times V_{BAT}$, the DS1307 internal power-switch circuit switches to battery power ($V_{BAT}$), maintaining the oscillator and 56 bytes of internal SRAM while consuming less than 500 nA.*
* **Q: Can the 3.3V STM32 communicate safely with the 5V DS1307?**
  * *A: Yes. `PB6` and `PB7` on the STM32F103 are 5V-tolerant (FT) pins. When configured in open-drain mode with pull-up resistors tied to 3.3V, the high logic level safely conforms to both devices' I2C $V_{IH}$ thresholds.*

