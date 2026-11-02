# Project Overview & System Architecture

## 1. Project Title & Purpose
**Smart Pill Box Reminder & Medication Safety Monitor**

The Smart Pill Box is an embedded healthcare assistance device built on the **STM32F103C8T6 (ARM Cortex-M3)** microcontroller. Its primary purpose is to:
1. Provide accurate, real-world scheduled medication alarms for patients taking multiple daily doses.
2. Ensure patient adherence through audio-visual reminders (SSD1306 OLED, LEDs, Piezo buzzer).
3. Prevent dangerous medication errors using an active verification system ("Wrong Pill" alert mechanism).
4. Allow complete offline configuration through a simple 3-button human-machine interface (HMI).
5. Maintain long-term real time using a battery-backed DS1307 Real-Time Clock module over I2C.

---

## 2. Hardware Architecture & Block Diagram

```mermaid
graph TD
    subgraph Controller [STM32F103C8T6 Blue Pill]
        I2C1[I2C1 Peripheral: PB6 SCL, PB7 SDA]
        GPIO_IN[Push Buttons: PB12, PB14, PB15]
        GPIO_OUT[Indicators: PB1 LED1, PB10 LED2, PB0 Buzzer]
        SYS_TICK[HAL SysTick / Timer: 1ms]
    end

    subgraph I2C_Bus [Shared I2C Bus - 100 kHz]
        OLED[SSD1306 128x64 OLED Display - Addr: 0x3C / 0x78]
        RTC[DS1307 Real-Time Clock Module - Addr: 0x68 / 0xD0]
    end

    subgraph Inputs [User Input Controls]
        BTN1[Button 1: Dose 1 Confirm / Menu Increment]
        BTN2[Button 2: Dose 2 Confirm / Menu Select]
        BTN3[Button 3: Setup Menu / Exit]
    end

    subgraph Outputs [Audio-Visual Feedback]
        LED1[Pill 1 Green LED]
        LED2[Pill 2 Yellow LED]
        BUZZ[Piezo Buzzer]
    end

    subgraph Power [Power Distribution]
        P3V3[3.3V Rail: STM32, OLED]
        P5V[5V Rail: DS1307 VCC]
        BATT[CR2032 3V Coin Cell: DS1307 Backup]
    end

    I2C1 <==> I2C_Bus
    GPIO_IN <== Inputs
    GPIO_OUT ==> Outputs
    Power -.-> Controller
    Power -.-> I2C_Bus
```

---

## 3. System Wiring & Pinout Table

| Peripheral | Signal | STM32 Pin | Mode | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **I2C1** | SCL | `PB6` | AF Open-Drain + External Pullup | Clock line for OLED & DS1307 |
| **I2C1** | SDA | `PB7` | AF Open-Drain + External Pullup | Data line for OLED & DS1307 |
| **Button 1** | Input | `PB12` | GPIO Input (Internal Pull-Up) | Pill 1 OK / Value Increment (+) |
| **Button 2** | Input | `PB14` | GPIO Input (Internal Pull-Up) | Pill 2 OK / Menu Item Select / Next |
| **Button 3** | Input | `PB15` | GPIO Input (Internal Pull-Up) | Setup Menu Open / Exit |
| **Pill 1 LED** | Output | `PB1` | GPIO Push-Pull Output | Pill 1 Visual Reminder Indicator |
| **Pill 2 LED** | Output | `PB10` | GPIO Push-Pull Output | Pill 2 Visual Reminder Indicator |
| **Buzzer** | Output | `PB0` | GPIO Push-Pull Output | Audible Frequency Alert |
| **ST-Link v2** | SWDIO | `PA13` | Serial Wire Data IO | SWD Debug / Flashing |
| **ST-Link v2** | SWCLK | `PA14` | Serial Wire Clock | SWD Debug / Flashing |

---

## 4. Software State Machine Workflow

```mermaid
stateDiagram-v2
    [*] --> SPLASH: Power-On Reset
    SPLASH --> NORMAL_IDLE: 1.2s delay & I2C Init

    state NORMAL_IDLE {
        [*] --> Monitoring
        Monitoring --> Monitoring: Poll DS1307 every 500ms
    }

    NORMAL_IDLE --> REMINDER_DOSE_1: Time == Dose 1 Alarm
    NORMAL_IDLE --> REMINDER_DOSE_2: Time == Dose 2 Alarm
    NORMAL_IDLE --> SETUP_MENU: Button 3 Pressed

    state REMINDER_DOSE_1 {
        [*] --> Alerting_1: Normal Blink 500ms
        Alerting_1 --> Wrong_Button_1: Button 2 Pressed (Wrong)
        Wrong_Button_1 --> Wrong_Button_1: Fast Blink 150ms + Warning Screen
    }

    state REMINDER_DOSE_2 {
        [*] --> Alerting_2: Normal Blink 500ms
        Alerting_2 --> Wrong_Button_2: Button 1 Pressed (Wrong)
        Wrong_Button_2 --> Wrong_Button_2: Fast Blink 150ms + Warning Screen
    }

    REMINDER_DOSE_1 --> DOSE_CONFIRMED: Button 1 Pressed (Correct)
    REMINDER_DOSE_2 --> DOSE_CONFIRMED: Button 2 Pressed (Correct)

    DOSE_CONFIRMED --> NORMAL_IDLE: 2s Success Display

    state SETUP_MENU {
        [*] --> Navigating: B1=Cycle, B2=Choose
    }

    SETUP_MENU --> NORMAL_IDLE: Button 3 Pressed
    SETUP_MENU --> SETUP_EDIT_HOUR: Button 2 Pressed
    SETUP_EDIT_HOUR --> SETUP_EDIT_MINUTE: Button 2 Pressed (B1 increments)
    SETUP_EDIT_MINUTE --> SETUP_CONFIRM: Button 2 Pressed (B1 increments)
    SETUP_CONFIRM --> SETUP_FEEDBACK: Button 2 Confirms (B1 toggles Save/Cancel)
    SETUP_FEEDBACK --> NORMAL_IDLE: 1.5s Display Delay
```

---

## 5. Potential Evaluator Questions & Key Answers
* **Q: Why are both the OLED and RTC connected to the same pins (`PB6` and `PB7`)?**
  * *A: I2C is a shared bus protocol. Each peripheral has a distinct 7-bit hardware address (OLED: `0x3C`, RTC: `0x68`). The master initiates transfers by broadcasting the address, allowing multiple slaves on two wires.*
* **Q: What happens if the power cuts off?**
  * *A: The DS1307 module has a CR2032 coin battery backup, ensuring real time ticks continuously. When power restores, the STM32 immediately polls the RTC and retrieves the accurate wall-clock time.*
* **Q: How does the system prevent accidental double-dosing or multiple alarms in the same minute?**
  * *A: In the alarm evaluation logic, each dose struct holds `lastTriggeredDay`. Once triggered on day `X`, the alarm won't rearm until day `X+1` or until modified in the menu.*

