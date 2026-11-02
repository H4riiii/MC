# ST-LINK v2 Debugger, SWD Protocol & Embedded In-Circuit Emulation

## 1. Overview of ST-LINK v2 & In-Circuit Debugging
The **ST-LINK v2** is an in-circuit debugger and programmer for the STM8 and STM32 microcontroller families. Rather than running a traditional software monitor or bootloader on the target, the ST-LINK communicates directly with hardware debugging silicon integrated on the ARM Cortex-M3 die through the **ARM CoreSight Serial Wire Debug (SWD)** interface.

---

## 2. SWD Physical Layer & Protocol Specification

```
         HOST PC (STM32CubeIDE)
                  | (USB 2.0 Full-Speed 12 Mbps)
            [ST-LINK v2]
                  |
     +------------+------------+
     |                         |
   SWDIO (PA13)              SWCLK (PA14)
(Bidirectional Data)        (Clock: 1-4 MHz)
```

### 2.1 Comparison: SWD vs. JTAG (IEEE 1149.1)

| Parameter | SWD (Serial Wire Debug) | JTAG (Joint Test Action Group) |
| :--- | :--- | :--- |
| **Dedicated Pins** | **2 pins** (`SWDIO`, `SWCLK`) | **4 to 5 pins** (`TDI`, `TDO`, `TCK`, `TMS`, `nTRST`) |
| **Pins Saved for Application** | **Frees 3 MCU pins** for GPIOs/Peripherals | Consumes valuable I/O pins |
| **Maximum Data Rate** | Up to 4.0 MHz clock frequency | Typically 1–10 MHz |
| **Boundary Scan Testing** | Not supported | Supported (ASIC pin-level PCB testing) |
| **Protocol Framing** | Packet-based with parity & ACK | Shift-register based state machine (TAP) |
| **Silicon Implementation** | ARM CoreSight proprietary DP/AP | Standard IEEE 1149.1 Test Access Port |

### 2.2 SWD Packet Structure & Electrical Turnaround
Every SWD transaction is synchronous to `SWCLK` and consists of three phases:
1. **8-bit Packet Request (Host $\rightarrow$ Target)**:
   - Start bit (1)
   - APnDP bit (0 = Debug Port, 1 = Access Port)
   - RnW bit (0 = Write, 1 = Read)
   - 2-bit Address (`A[3:2]`)
   - Even Parity bit
   - Stop bit (0) + Park bit (1)
2. **Turnaround (`Trn`) Period**: 1 clock cycle where neither device drives the line.
3. **3-bit ACK Response (Target $\rightarrow$ Host)**:
   - `001` = **OK** (Success)
   - `010` = **WAIT** (Target busy)
   - `100` = **FAULT** (Transfer error)
4. **32-bit Data Phase + Parity**: The payload transmitted with LSB first.

---

## 3. Physical Wiring to the STM32 Blue Pill

| ST-LINK Pin | Blue Pill Pin | Description | Critical Precautions |
| :--- | :--- | :--- | :--- |
| **SWDIO** | `PA13` | Bidirectional Data | Keep wire length under 15 cm to prevent signal reflection |
| **SWCLK** | `PA14` | Synchronous Clock | Keep clear of fast-switching PWM or buzzer lines |
| **GND** | `GND` | Ground Reference | **MANDATORY**: Common reference prevents logic shift |
| **3.3V** | `3.3V` | Target Supply | Power target from ST-LINK OR USB (Never both!) |

> [!CAUTION]
> **Ground Loop & Dual-Power Hazard:** Connecting the ST-LINK 3.3V supply and the Blue Pill USB 5V cable simultaneously can back-feed voltage regulators, causing reverse current flow and thermal breakdown. For live debugging with USB connected, disconnect the ST-LINK 3.3V pin and leave only **SWDIO, SWCLK, and GND**.

---

## 4. Boot Selection Modes (`BOOT0` and `BOOT1`)

The STM32F103 boot configuration is evaluated on the fourth rising edge of `SYSCLK` after power-on reset:

| BOOT0 Jumper | BOOT1 Jumper | Boot Space | Starting Address | Target Application |
| :---: | :---: | :--- | :---: | :--- |
| **0 (Default)** | **X** | **Main Flash Memory** | `0x08000000` | Normal execution of compiled user firmware |
| **1** | **0** | **System Memory** | `0x1FFFF000` | Factory ST ROM bootloader (UART flashing via `PA9`/`PA10`) |
| **1** | **1** | **Embedded SRAM** | `0x20000000` | Direct RAM execution (diagnostic code development) |

* **Standard Project Setting:** Keep both **`BOOT0 = 0`** and **`BOOT1 = 0`**.

---

## 5. ARM Cortex-M3 On-Chip Hardware Debug Silicon

Unlike software debuggers that modify assembly instructions in RAM, the Cortex-M3 includes hardware debugging macrocells:
1. **FPB (Flash Patch and Breakpoint Unit)**:
   - Provides **6 hardware instruction comparators**.
   - Compares the Program Counter (PC) directly on the instruction bus. When a match occurs, it halts the CPU pipeline without modifying Flash memory.
2. **DWT (Data Watchpoint and Trace Unit)**:
   - Provides **4 data watchpoints**.
   - Monitors memory addresses (e.g. `&currentRtc`) and triggers a halt or trace packet when a variable is written or read.
3. **NVIC Vector Catch**:
   - Automatically halts the processor if a `HardFault`, `BusFault`, `UsageFault`, or `MemManage` exception occurs, displaying the exact faulted instruction address in STM32CubeIDE.

---

## 6. Evaluator Oral Questions & Detailed Answers

### Q1: What is the difference between a hardware breakpoint and a software breakpoint?
> **Answer**: 
> A **software breakpoint** works in RAM by replacing the target instruction with the ARM `BKPT` opcode (`0xBE00`). When the CPU reaches this opcode, it traps into debug mode. This cannot be done in Flash memory without an erase-write cycle. A **hardware breakpoint** uses the Cortex-M3 **FPB unit**, where dedicated hardware address comparators monitor the bus and trigger a core halt when the Program Counter equals the breakpoint address, leaving Flash memory completely unmodified.

### Q2: What should you do if the debugger returns "Cannot connect to target"?
> **Answer**: 
> Common causes and engineering remedies:
> 1. **Floating Ground**: Ensure ST-LINK GND is firmly seated to the Blue Pill GND rail.
> 2. **Disabled SWD Pins**: If code remaps `PA13`/`PA14` as standard GPIOs, the SWD port is locked out after boot.
> 3. **Deep Sleep**: If the MCU enters `STOP` or `STANDBY` mode, clocks to the debug port are gated off.
> 4. **Fix (Connect Under Reset)**: Hold the Blue Pill onboard `RESET` button, initiate debug launch in STM32CubeIDE set to "Software System Reset / Connect Under Reset", and release the button once the ST-LINK establishes the hardware handshake.

### Q3: How does the debugger inspect variables in real time without pausing code execution?
> **Answer**: 
> Through the **CoreSight Memory Access Port (MEM-AP)**. The MEM-AP has direct access to the internal AHB system bus, allowing it to read and write SRAM locations (like `currentRtc` or `currentState`) during idle bus cycles without stealing clock cycles from the Cortex-M3 CPU core (Live Expressions).
