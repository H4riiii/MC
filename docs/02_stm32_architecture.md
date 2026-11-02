# STM32 Microcontroller Architecture & Electrical Interface

## 1. Overview of STM32F103C8T6 ("Blue Pill")
The STM32F103C8T6 is an entry-level high-performance 32-bit microcontroller manufactured by STMicroelectronics. It features the **ARM Cortex-M3 core (ARMv7-M architecture)** utilizing a 3-stage instruction pipeline (Fetch, Decode, Execute) with Harvard architecture (separate instruction and data buses: ICode and DCode).

---

## 2. Electrical Specifications & Memory Footprint

| Parameter | Datasheet Rating | Project Usage & Safety Limits |
| :--- | :--- | :--- |
| **CPU Core** | 32-bit ARM Cortex-M3 (RISC) | 72 MHz SYSCLK operating frequency |
| **Flash Memory** | 64 KB (128 KB factory unlocked) | Binary size: ~29 KB (leaves >50% headroom) |
| **SRAM** | 20 KB (20,480 bytes) | Uses ~4.6 KB (1 KB display buffer + variables) |
| **Supply Voltage ($V_{DD}$)** | 2.0 V to 3.6 V | Powered at 3.3V via onboard RT9193 LDO regulator |
| **Max GPIO Current Output** | 25 mA per pin | LEDs limited to ~8 mA with $330\,\Omega$ series resistors |
| **Total MCU Current Limit** | 150 mA sum of all I/Os | Entire system draws ~45 mA at full OLED brightness |
| **5V Tolerant (FT) Pins** | Up to 5.5V without damage | `PB6`, `PB7`, `PB12`, `PB14`, `PB15` are FT rated |
| **Non-5V Tolerant Pins** | Max $V_{DD} + 0.3\text{V} \approx 3.6\text{V}$ | `PA0`-`PA7` (ADC channels), `PB0`, `PB1` |

---

## 3. Clock Distribution Network & Phase-Locked Loop (PLL)

The STM32F103 clock tree configuration determines CPU execution speed and peripheral communication rates:

```
  [8.000 MHz Crystal] (HSE) 
           |
       [PREDIV1: /1]
           |
         [PLL: x9 Multiplier]  --->  SYSCLK = 72 MHz
                                         |
     +-----------------------------------+-----------------------------------+
     |                                   |                                   |
[AHB Prescaler: /1]             [APB1 Prescaler: /2]                [APB2 Prescaler: /1]
     |                                   |                                   |
 HCLK = 72 MHz                       PCLK1 = 36 MHz                      PCLK2 = 72 MHz
(Cortex Core, DMA, Flash)          (I2C1, Timers 2,3,4)               (GPIO A/B/C, SPI1, ADC)
```

### 3.1 Mathematical Clock Derivations
1. **System Clock ($f_{\text{SYSCLK}}$)**:
   $$f_{\text{SYSCLK}} = f_{\text{HSE}} \times 9 = 8.000\,\text{MHz} \times 9 = 72.000\,\text{MHz}$$
2. **APB1 Peripheral Clock ($f_{\text{PCLK1}}$)**:
   $$f_{\text{PCLK1}} = \frac{f_{\text{HCLK}}}{2} = \frac{72.000\,\text{MHz}}{2} = 36.000\,\text{MHz}\quad (\text{Max allowed for APB1 is } 36\,\text{MHz})$$
3. **Flash Memory Latency (Wait States)**:
   Flash access time is limited to $\le 24\,\text{MHz}$. At 72 MHz:
   $$\text{Required Wait States} = \left\lceil \frac{72\,\text{MHz}}{24\,\text{MHz}} \right\rceil - 1 = 3 - 1 = 2\text{ Wait States (FLASH\_LATENCY\_2)}$$
   Failing to configure 2 wait states causes instruction prefetch memory corruption and immediate `HardFault_Handler` execution.

---

## 4. GPIO Electrical Architecture: Push-Pull vs. Open-Drain

Inside each STM32 GPIO pin cell, two complementary power MOSFETs control pin state:

```
          VDD (3.3V or 5V FT)
             |
          [P-MOS]  <--- Driven by Output Control Logic
             |
 Pin ----+---+
         |
      [N-MOS]      <--- Driven by Output Control Logic
         |
        GND
```

### 4.1 Push-Pull Output Mode (`GPIO_MODE_OUTPUT_PP`)
* **Operation**: P-MOS conducts for Logic HIGH (3.3V); N-MOS conducts for Logic LOW (0V).
* **Used for**: `PB1` (Pill 1 LED), `PB10` (Pill 2 LED), `PB0` (Buzzer), `PA4` (SD CS), `PA5` (SCK), and `PA7` (MOSI).
* **Advantage**: Fast rise and fall times with symmetric drive capability ($25\,\text{mA}$).

### 4.2 Open-Drain Output Mode (`GPIO_MODE_AF_OD`)
* **Operation**: P-MOS is permanently deactivated. Pin can only pull to GND (N-MOS active) or enter High-Impedance (Hi-Z) when N-MOS is off. An external pull-up resistor pulls the bus to HIGH.
* **Used for**: `PB6` (I2C1 SCL) and `PB7` (I2C1 SDA).
* **Why it is Mandatory for I2C**: Enables **wired-AND** logic. If slave and master both transmit simultaneously, no high-current contention occurs. Slaves can also hold SCL LOW to request clock stretching.

---

## 5. Five-Volt Tolerance (FT) vs. Non-FT Silicon Physics

```
   NON-FT PIN (e.g. PA6 / MISO):            FT PIN (e.g. PB6 / SCL):
   
         VDD (3.3V)                                VDD (3.3V)
             |                                         |
          [Diode]  (Upper Clamp)                     [None]  (No Upper Diode)
             |                                         |
 Pin --------+------- Analog/Digital       Pin --------+------- Open-Drain Stage
             |                                         |
          [Diode]  (Lower Clamp)                    [Diode]  (Lower Clamp)
             |                                         |
            GND                                       GND
```

* **Non-FT Pins (`PA0`-`PA7`, `PB0`, `PB1`)**: Incorporate an upper internal protection clamping diode tied to $V_{DD}$ (3.3V). Applying $5\text{V}$ forward-biases this diode, causing high parasitic current into the 3.3V supply rail, latch-up, and thermal destruction of the analog multiplexer.
* **FT Pins (`PB6`, `PB7`, `PB12`, `PB14`, `PB15`)**: Have no upper diode connected to $V_{DD}$. Instead, they use a high-voltage N-MOS with a deep N-well breakdown rating exceeding $6.0\text{V}$, allowing safe direct connection to 5V pull-ups when configured in open-drain mode.

---

## 6. Evaluator Oral Questions & Detailed Answers

### Q1: What is the RC rise-time equation for the I2C bus pull-up resistors?
> **Answer**: 
> In I2C open-drain mode, the rising edge is passive and governed by an RC charging curve:
> $$V(t) = V_{DD} \left(1 - e^{-t / (R_p C_b)}\right)$$
> The standard I2C specification defines rise time $t_r$ from $0.3\,V_{DD}$ to $0.7\,V_{DD}$:
> $$t_r \approx 0.8473 \times R_p \times C_b$$
> For Standard Mode ($100\,\text{kHz}$), the maximum permitted rise time is $t_{r,\max} = 1000\,\text{ns}$. With a bus capacitance $C_b \approx 50\,\text{pF}$ (PCB trace + OLED + RTC pins), a $4.7\,\text{k}\Omega$ pull-up yields:
> $$t_r = 0.8473 \times 4700 \times 50 \times 10^{-12} \approx 199\,\text{ns} \ll 1000\,\text{ns}$$
> This provides crisp square waves with zero bit distortion.

### Q2: Why did you not use interrupts (EXTI) for button detection?
> **Answer**: 
> Mechanical pushbuttons exhibit contact bounce lasting $5\text{ ms}$ to $25\text{ ms}$. If mapped directly to External Interrupts (`EXTI12`, `EXTI14`, `EXTI15`), a single press generates dozens of rapid nested ISR triggers, starving the main loop and causing stack overflow. Instead, we use a **10 ms periodic polling with software timestamp edge detection**:
> ```c
> if (lastState == HIGH && currentState == LOW) {
>     if (now - lastPressTime > 200) { /* Process valid click */ }
> }
> ```
> This is completely deterministic, consumes zero interrupt overhead, and eliminates bounce completely.

### Q3: How is the SysTick hardware timer configured in this project?
> **Answer**: 
> SysTick is a 24-bit downcounter clocked by $HCLK = 72\,\text{MHz}$. The reload register is programmed to:
> $$\text{LOAD} = \frac{72\,\text{MHz}}{1000\,\text{Hz}} - 1 = 71,999$$
> When the counter reaches 0, it asserts the `SysTick_Handler` vector every $1.000\,\text{ms}$, incrementing `uwTick`. This gives `HAL_GetTick()` a millisecond timebase for all non-blocking timers.
