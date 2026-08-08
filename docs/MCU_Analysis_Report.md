# Low-Power LoRaWAN Data Vault & Wakeup Manager
## System Architecture Design & Microcontroller Comparison Study

---

## 1. Executive Summary
This document defines the architectural strategy for maximizing battery longevity in a solar-harvesting internet-of-Things (IoT) device. The core design paradigm isolates the highly complex, power-heavy main Application Processor from continuous standby power consumption. 

Instead of maintaining the primary microcontroller (MCU) in a standard low-power sleep state—where internal static leakage current continuously drops battery voltage—the main MCU is **completely unpowered (0V)** during idle intervals.

An auxiliary, ultra-low-power "Data Vault" microcontroller remains active at a sub-microamp threshold. This secondary MCU is strictly responsible for two critical operations:
1. **Timekeeping:** Running a Real-Time Clock (RTC) tracking interval timing to trigger system events.
2. **Context Retention:** Storing the transient session parameters of the LoRaWAN communication stack (Network keys, application keys, frame counters) inside volatile but retained memory.

Upon timer expiration, the auxiliary MCU boots the primary system rail, transfers the network context over an **I2C interface**, and safely monitors the pipeline until the transmission cycle concludes.

---

## 2. Technical Challenge: The LoRaWAN Context Footprint
When a LoRaWAN endpoint loses power, it loses its network authorization status unless it triggers a new, power-intensive Over-The-Air Activation (OTAA) join sequence on every boot. To bypass this overhead, a standard LoRaWAN 1.0.x / 1.1 endpoint session must be preserved statically. 

The bare minimum variables required to safely bypass a network re-join sequence map to exactly **40 bytes**:
*   **DevAddr** (Device Address): 4 Bytes
*   **NwkSKey** (Network Session Key): 16 Bytes
*   **AppSKey** (Application Session Key): 16 Bytes
*   **FCntUp** (Uplink Frame Counter): 4 Bytes

### Production Overhead Consideration
While the foundational mathematical keys occupy 40 bytes, fully functional open-source stacks (such as *LoRaMAC-node* or *MCCI LMIC*) track secondary operational parameters. These include adaptive data rates (ADR), link-check frame tokens, and channel frequency masks. To maintain a transparent, drop-in state recovery without forcing the main MCU to run lengthy recalculations, the retention target expands to a range of **256 Bytes to 512 Bytes**.

---

## 3. Microcontroller Comparative Analysis
To select the ultimate hardware companion for this specific role, multiple generations and families of microcontrollers were comprehensively audited based on three criteria: **Active RTC Sleep Current**, **Data Retention Capabilities**, and **Pin Management Constraints**.

| Microcontroller Part Number | Key Architectural Core | Memory Retention Mechanism | Sleep Power (RTC Active) | Architecture Trade-offs & Limitations |
| :--- | :--- | :--- | :--- | :--- |
| **NXP LPC810** | Arm Cortex-M0+ | Power-down Mode (SRAM Alive) | ~2.0 µA – 3.5 µA | **Severe Pin Constraints:** 8-pin DIP/SOP package leaves only 6 I/Os. Running an external crystal leaves zero pins for I2C and hardware debug lines. Legacy technology with higher static current leakage. |
| **NXP LPC81xM Series** | Arm Cortex-M0+ | Power-down Mode (SRAM Alive) | ~1.5 µA – 3.0 µA | Base architecture contains a flexible Switch Matrix. However, the internal low-power oscillators exhibit high thermal drift, making down-link window synchronization for LoRaWAN highly volatile. |
| **STMicroelectronics STM32C011J6M6** | Arm Cortex-M0+ | Standby Mode (Backup Registers) | ~7.45 µA | **Budget Optimized Core:** Built to compete with cheap 8-bit MCUs. It lacks advanced low-power process nodes. Dropping into deep "Shutdown Mode" reduces draw to 20 nA but wipes all backup registers completely. |
| **STMicroelectronics STM32L4 Series** | Arm Cortex-M4 with FPU | Stop 2 Mode (SRAM Retained) | ~1.1 µA – 1.4 µA | High performance core featuring cache optimization. Perfect for micro-second wake times, but the larger gate count of the M4 core translates to an unnecessary active power baseline during simple I2C transfers. |
| **STMicroelectronics STM32U031F8P6** | Arm Cortex-M0+ | Stop 2 Mode (Full SRAM Retained) | **~630 nA (0.63 µA)** | **Optimal Selection:** Purpose-built for companion power-management tasks. Preserves all 12 KB of internal SRAM without relying on limited backup registers or wearing out flash memory sectors. |

---

## 4. Final Selected Hardware Audit: STM32U031F8P6
The selection of the **STM32U031F8P6** represents the absolute technical sweet spot for this solar-recharging deployment. 

### Key Technical Assets:
*   **Package Factor (20-Pin TSSOP):** Resolves the critical pin bottleneck introduced by early 8-pin evaluation choices. It exposes 16 usable I/Os, allowing simultaneous execution of an external low-power crystal (LSE), an independent I2C bus, a dedicated hardware wake-up line, and functional Serial Wire Debug (SWD) developer tracking pins.
*   **Advanced Power-Gating (Stop 2 Mode):** The chip achieves a sub-microamp consumption of **~630 nA** while powering down its computational core, clock trees, and high-speed buses. Crucially, it leaves its internal **12 KB SRAM pool fully energized**. 
*   **Data Safety:** Because the state variables stay native to the SRAM during Stop 2 sleep, the firmware avoids writing the changing Frame Counter (`FCntUp`) back to internal non-volatile Flash memory. This cleanly avoids the physical wear-out limits (10k to 100k cycles) that would otherwise destroy a flash-reliant architecture within a few years of constant transmission intervals.

---

## 5. System Design & Electrical Mitigation Strategies
When interfacing an unpowered microcontroller (0V VCC) with a live auxiliary microcontroller over a standard communication bus, unique electrical failures will occur unless mitigated at the schematic and firmware level.

### 1. Parasitic Back-Powering Prevention
If the primary MCU loses power while the secondary MCU continues to drive I2C lines (SDA/SCL) high, current will leak from the secondary GPIOs through the internal Electrostatic Discharge (ESD) protection diodes of the main MCU. This causes two system failures:
1. It partially and unpredictably turns on the main MCU's internal power domains, leading to erratic latch-up states.
2. It causes a massive current drain on the battery, instantly voiding the sub-microamp sleep goals.

**Firmware Mitigation Strategy:** Before entering the Stop 2 state, the auxiliary MCU must explicitly call `HAL_I2C_MspDeInit()` to de-initialize the I2C block. The corresponding GPIO pins must be explicitly reconfigured as **Analog Input with No Pull-up and No Pull-down**. This drops the physical lines into a high-impedance state, safely decoupling the power domains.

### 2. Pull-Up Resistor Placement Matrix
Traditional I2C communication requires pull-up resistors on both the clock and data lines to maintain a high idle state. 
*   **The Trap:** If these resistors are tied to the auxiliary MCU's permanent 3.3V battery rail, current will constantly bleed to ground whenever an MCU pin toggles or settles low during sleep.
*   **The Fix:** Physical I2C pull-up resistors must be wired directly to the **switched power domain of the main Application Processor**. When the primary system regulator is shut down, the pull-up voltage rail falls naturally to 0V. The I2C lines gracefully rest at ground without pulling a single nano-amp of parasitic energy from the secondary battery management line.

---

## 6. Abstract Operational Firmware Loop
Unlike lower-tier Standby modes which trigger a full system reset upon wake-up, entering **Stop 2 Mode** allows the STM32U031F8P6 to preserve the CPU state. When the internal RTC hardware timer fires an alert, execution picks up cleanly on the immediate next line of code, simplifying system tracking:

```c
// Global runtime container kept safely alive inside the retained 12KB SRAM
LoRaWAN_Context_t globalLoRaStack;

int main(void) {
    // Standard Early-Boot Local Peripherals Local Initialisation
    HAL_Init();
    SystemClock_Config();
    MX_RTC_Init();
    
    while(1) {
        /* ========================================================
           STATE 1: CORE AWAKE & INITIALISING SYSTEM WAKEUP
           ======================================================== */
        // Signal the primary power regulator rail to turn on the Main MCU
        HAL_GPIO_WritePin(GPIOA, MAIN_MCU_REGULATOR_EN_PIN, GPIO_PIN_SET);
        
        // Re-enable I2C hardware internal clock trees and peripheral blocks
        MX_I2C1_Init(); 
        
        // Block and listen as an I2C Slave. 
        // 1. Main MCU reads the stored 'globalLoRaStack' context variables.
        // 2. Main MCU runs sensor sweeps and completes its LoRa transmission.
        // 3. Main MCU writes back the incremented frame counters to 'globalLoRaStack'.
        Execute_I2C_Slave_Data_Exchange();
        
        /* ========================================================
           STATE 2: BUS ISOLATION & PRE-SLEEP STAGING
           ======================================================== */
        // Turn off I2C module and force physical pins to High-Impedance Analog
        HAL_I2C_MspDeInit(&hi2c1);
        Isolate_GPIO_Pins_To_Analog_LowPower();
        
        // Define the exact next wake-up boundary inside the internal hardware RTC
        Configure_Next_RTC_Wakeup_Alarm(INTERVAL_SECONDS);
        
        // Clear power management wake-up registers and tracking flags
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
        
        // Suspend the internal core 1ms SysTick timebase interrupt
        HAL_SuspendTick();
        
        /* ========================================================
           STATE 3: ENTER DEEP STOP 2 POWER-GATING (Current drops to ~630 nA)
           ======================================================== */
        HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
        
        /* ========================================================
           STATE 4: SYSTEM RESUMPTION (RTC Alarm Triggered Interrupt)
           ======================================================== */
        // Core picks up execution cleanly right here. Bring clock domains back online.
        HAL_ResumeTick();
        SystemClock_Config();
    }
}
```
