#include "vault/platform.h"
#include "vault/vault_i2c_registers.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_i2c.h"

/* PD6 (SDA)/PD7 (SCL) -- the same pins platform_efm32g210_gpio.c uses for
   I2C_SDA_PORT/PIN and I2C_SCL_PORT/PIN (kept as separate local defines
   here since those are file-static in that translation unit). */
#define I2C_SDA_PORT gpioPortD
#define I2C_SDA_PIN  6
#define I2C_SCL_PORT gpioPortD
#define I2C_SCL_PIN  7

/* I2C0 ROUTE LOCATION for PD6 (SDA)/PD7 (SCL).
 *
 * Verified against the real vendored headers, not guessed:
 *   vendor/Gecko_SDK/platform/Device/SiliconLabs/EFM32G/Include/efm32g_af_pins.h:
 *     AF_I2C0_SDA_PIN(i)  = i==0:0  i==1:6  i==2:6  i==3:14
 *     AF_I2C0_SCL_PIN(i)  = i==0:1  i==1:7  i==2:7  i==3:15
 *   efm32g_af_ports.h:
 *     AF_I2C0_SDA_PORT(i) = i==0:0(A) i==1:3(D) i==2:2(C) i==3:3(D)
 *     AF_I2C0_SCL_PORT(i) = i==0:0(A) i==1:3(D) i==2:2(C) i==3:3(D)
 * Location index 1 is the only one that yields port D (gpioPortD == 3,
 * per em_gpio.h's GPIO_Port_TypeDef) pins 6/7 -- i.e. PD6/PD7 -- matching
 * this board's I2C0_SDA/I2C0_SCL silkscreen labels. The brief's guessed
 * LOCATION 0 (LOC0) actually maps to PA0/PA1, which is wrong for this
 * board; LOC1 is the correct value, confirmed against
 * efm32g210f128.h's I2C_ROUTE_LOCATION_LOC1 macro. */
#define I2C0_ROUTE_LOCATION I2C_ROUTE_LOCATION_LOC1

void platform_i2c_slave_init(uint8_t addr) {
    CMU_ClockEnable(cmuClock_I2C0, true);

    /* Route SDA/SCL to PD6/PD7 and enable the pins in the ROUTE
       register. Field names (I2C_ROUTE_SDAPEN, I2C_ROUTE_SCLPEN,
       I2C_ROUTE_LOCATION_LOCn) confirmed against efm32g210f128.h. */
    I2C0->ROUTE = I2C_ROUTE_SDAPEN | I2C_ROUTE_SCLPEN | I2C0_ROUTE_LOCATION;

    /* Reconfigure PD6/PD7 from the high-impedance isolation state
       platform_bus_isolate() (Task 3) leaves them in, into the I2C
       peripheral's required open-drain-with-pull-up mode
       (gpioModeWiredAndPullUp, confirmed in em_gpio.h's
       GPIO_Mode_TypeDef -- I2C is a wired-AND/open-drain bus, it must
       never be driven push-pull). `out`=1 so DOUT is high (released,
       i.e. not actively pulling the line low) when the pin isn't
       actively clocking/ack'ing low. */
    GPIO_PinModeSet(I2C_SDA_PORT, I2C_SDA_PIN, gpioModeWiredAndPullUp, 1);
    GPIO_PinModeSet(I2C_SCL_PORT, I2C_SCL_PIN, gpioModeWiredAndPullUp, 1);

    /* Slave address in bits [7:1] -- confirmed against efm32g210f128.h's
       I2C_SADDR bitfields (_I2C_SADDR_ADDR_SHIFT == 1,
       _I2C_SADDR_ADDR_MASK == 0xFE), matching the brief's shift. */
    I2C0->SADDR = (uint32_t)(addr << 1);

    /* CLKDIV: the vendored em_i2c.c (I2C_BusFreqSet) carries an explicit
       comment -- "The clock divisor must be at least 1 in slave mode
       according to the reference manual" -- and forces div=1 whenever
       CTRL.SLAVE is set and the computed divisor would otherwise be 0.
       This driver never calls I2C_BusFreqSet (no master-mode frequency
       to compute; slave mode tracks the master's clock), but CLKDIV
       still resets to 0, which is exactly the value that comment says is
       invalid for slave mode -- so it must be set explicitly here rather
       than left at its reset default. */
    I2C0->CLKDIV = 1;

    /* Clear any stale flags before enabling, matching the order emlib's
       own I2C_Init() uses (IEN=0 + IntClear before touching CTRL/state),
       so a leftover flag from a previous init/deinit cycle can't
       immediately re-trigger the IRQ once enabled below. */
    I2C0->IFC = _I2C_IFC_MASK;

    /* Enable the peripheral in SLAVE mode with AUTOACK.
     *
     * I2C_CTRL_SLAVE (bit 1, "Addressable as Slave") and I2C_CTRL_AUTOACK
     * (bit 2, "Automatic Acknowledge") are both confirmed field names in
     * efm32g210f128.h. Both are load-bearing and were missing from the
     * brief's sketch, which set only I2C_CTRL_EN:
     *
     *   - Without CTRL_SLAVE, the peripheral does not respond to its own
     *     slave address at all -- I2C_STATE_MASTER-relative slave
     *     matching per the reference manual's I2C chapter requires this
     *     bit; em_i2c.c's own I2C_Init() sets it via
     *     BUS_RegBitWrite(&i2c->CTRL, _I2C_CTRL_SLAVE_SHIFT, !init->master).
     *   - Without CTRL_AUTOACK, this part's I2C slave state machine
     *     clock-stretches (holds SCL low) after every address match and
     *     every received data byte, waiting for firmware to explicitly
     *     write CMD.ACK or CMD.NACK (see I2C_STATUS.PACK/"Pending ACK" in
     *     efm32g210f128.h) before it will continue -- this driver's IRQ
     *     handler never writes CMD, so without AUTOACK the bus would
     *     hang permanently on the first address match. AUTOACK makes the
     *     hardware generate that ACK itself for received address/data,
     *     matching how this handler is actually written (it only reads
     *     RXDATA / writes TXDATA / handles STOP, never touches CMD). */
    I2C0->CTRL |= I2C_CTRL_SLAVE | I2C_CTRL_AUTOACK | I2C_CTRL_EN;

    /* Enable the relevant interrupt sources: address match, RX data
       valid, TX buffer level, and slave STOP. I2C_IEN_TXBL is required
       here even though it's not mentioned in the brief's IEN write --
       the handler below acts on I2C_IF_TXBL, but IF reflects raw
       peripheral status regardless of IEN; if TXBL is the only pending
       condition during a master-read transaction and it isn't enabled
       in IEN, no interrupt is ever raised and I2C0_IRQHandler never
       runs, so no byte is ever shipped to the master -- a silent read-
       side hang. Field names (I2C_IEN_ADDR, I2C_IEN_RXDATAV,
       I2C_IEN_TXBL, I2C_IEN_SSTOP) confirmed against efm32g210f128.h. */
    I2C0->IEN = I2C_IEN_ADDR | I2C_IEN_RXDATAV | I2C_IEN_TXBL | I2C_IEN_SSTOP;

    NVIC_EnableIRQ(I2C0_IRQn);
}

void platform_i2c_slave_deinit(void) {
    NVIC_DisableIRQ(I2C0_IRQn);
    I2C0->IEN = 0;
    I2C0->CTRL &= ~(I2C_CTRL_EN | I2C_CTRL_SLAVE | I2C_CTRL_AUTOACK);
    I2C0->ROUTE = 0;
    CMU_ClockEnable(cmuClock_I2C0, false);
}

void platform_wait_for_interrupt(void) {
    /* Plain WFI. EFM32's deep-sleep entry (EMU_EnterEM2()/EM3, Task 6's
       platform_enter_low_power_sleep()) does not exist yet in this
       codebase, so there is no SLEEPDEEP-setting code anywhere in this
       backend today for this call to conflict with -- the SCB->SCR
       SLEEPDEEP bit is architecturally shared between plain WFI and deep
       sleep on Cortex-M3 exactly as it is on the Cortex-M0+ parts the
       LPC810/STM32U031 backends target (same ARMv7-M/ARMv6-M sleep-mode-
       selection mechanism), so the same hazard *would* apply once
       something sets SLEEPDEEP and leaves it set. But guarding against
       it here, preemptively, would be guarding against a bug that
       doesn't exist yet in a function that doesn't exist yet -- adding a
       speculative "clear SLEEPDEEP" here now cannot be verified against
       real deep-sleep-entry code and risks silently masking a real
       ordering bug when Task 6 lands. This is correctly Task 6's
       responsibility: platform_enter_low_power_sleep() must leave
       SLEEPDEEP clear on return (either by never setting it in the first
       place outside the WFI that enters EM2, or by explicitly clearing
       it before returning), the same fix already applied on the other
       two backends. */
    __WFI();
}

void I2C0_IRQHandler(void) {
    uint32_t flags = I2C0->IF;

    /* Decode order confirmed against efm32g210f128.h's I2C_IF bit
       layout: ADDR (bit 2), TXC (3), TXBL (4), RXDATAV (5), ACK (6),
       NACK (7), SSTOP (16) are all independently testable status bits,
       matching the general EFM32 I2C IF register convention the brief
       assumed. With CTRL_AUTOACK set (see platform_i2c_slave_init()),
       the hardware handles ACK/NACK generation for the address match and
       each received byte itself, so this handler only needs to react to
       ADDR/RXDATAV/TXBL/SSTOP -- it never needs to inspect
       I2C_STATE.TRANSMITTER or write CMD.ACK/NACK. */
    if (flags & I2C_IF_ADDR) {
        I2C0->IFC = I2C_IFC_ADDR;
    }
    if (flags & I2C_IF_RXDATAV) {
        vault_i2c_registers_on_write_byte((uint8_t)I2C0->RXDATA);
    }
    if (flags & I2C_IF_TXBL) {
        I2C0->TXDATA = vault_i2c_registers_on_read_request();
    }
    if (flags & I2C_IF_SSTOP) {
        I2C0->IFC = I2C_IFC_SSTOP;
        vault_i2c_registers_on_stop();
    }
}
