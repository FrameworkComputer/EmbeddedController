/* -*- mode:c -*-
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This file describes GPIO mapping for the cr50 code running on the H1 chip.
 *
 * For the purposes of this file H1 core has the following logical and
 * physical items and properties:
 *
 *   - 32 internal GPIOs, which are split into two ports of 16 bits each.
 *     Ports' architecture and programmig is described in "ARM Cortex-M System
 *     Design Kit TRM" DDIO47B.
 *
 *   - a set of peripherals - slave and master SPI and I2C controllers, UARTs,
 *     interrupt controller, etc.
 *
 *   - 28 pins on the package named DIOA0..14, DIOB0..7 and DIOM0..4
 *
 *   - a PINMUX - a unit which allows to interconnect objects from the three
 *     groups listed above. Note that some peripherals are attached to some
 *     pins directly, so in case those peripherals are used the pins should
 *     not be connected by PINMUX to any other outputs.
 *
 * The below macros are somewhat misleading (apparently for historical
 * reasons), as PIN(p, b) component in fact refers not to the external pin,
 * but to the GPIO (bit b on port p), where bit is in 0..15 range, and port is
 * in 0..1 range.
 *
 * To describe routing of an external signal two macro instantiations are
 * required:
 *
 * The GPIO_INT() or GPIO() macro assigns the signal a name and assigns it to
 * the internal GPIO port, (again, defining the port using the PIN(port, bit)
 * component of the macro invocation). GPIO_INT definitions assign their
 * respective signals to interrupts and ISRs.
 *
 * The PINMUX macro assigns the previously defined GPIO to another object,
 * most commonly to an external pin, but possibly to some internal component.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/*
 * A manually maintained tally of all GPIO ports configured in this file.
 *
 * GPIO0.0   int_ap_l
 * GPIO0.1   ec_flash_select
 * GPIO0.2   ap_flash_select
 * GPIO0.4   sys_rst_l_out
 * GPIO0.5   ccd_mode_l
 * GPIO0.6   batt_pres_l
 * GPIO0.7   spi_mosi
 * GPIO0.8   spi_clk
 * GPIO0.9   spi_cs_l
 * GPIO0.10  diob4
 * GPIO0.11  en_pp3300_ina
 * GPIO0.12  i2c_scl_ina
 * GPIO0.13  i2c_sda_ina
 * GPIO0.14  i2cs_sda poll
 * GPIO0.15  ec_tx_cr50_rx_out
 * GPIO1.0   tpm_rst_l
 * GPIO1.1   detect_ap_uart
 * GPIO1.2   detect_ec_uart
 * GPIO1.3   detect_servo
 * GPIO1.4   detect_tpm_rst_asserted
 * GPIO1.5   unwedge_i2cs_scl
 * GPIO1.6   monitor_i2cs_sda
 * GPIO1.11  ec_tx_cr50_rx_in
 * GPIO1.12  strap_a0
 * GPIO1.13  strap_a1
 * GPIO1.14  strap_b0
 * GPIO1.15  strap_b1
 */
/*****************************************************************************/
/* INTERRUPT GPIOs - interrupts processed in chip/g/gpio.c */
/*
 * The system reset signal can be from two different pins depending on what the
 * board type is. One board uses plt_rst_l (diom3) and the other board type uses
 * sys_rst_l (diom0) to detect a warm reset. The pin is selected based on the
 * board properties in board.c
 *
 * On both boards sys_rst_l is used as an output to trigger warm resets. The ARM
 * core can't trigger an interrupt if it's driving it as an output so we attach
 * two internal GPIOs to the same pad if sys_rst_l is also being used to detect
 * system resets.
 */
GPIO_INT(TPM_RST_L,       PIN(1, 0),  GPIO_INT_RISING, tpm_rst_deasserted)
GPIO_INT(DETECT_AP_UART,  PIN(1, 1),  GPIO_INT_HIGH, ap_detect_asserted)
GPIO_INT(DETECT_EC_UART,  PIN(1, 2),  GPIO_INT_HIGH, ec_detect_asserted)
/*
 * DETECT_SERVO and EC_TX_CR50_RX pins must NOT be changed without also changing
 * the pinmux_regval fields in the bitbang_config in board.c.  The pinmux values
 * in the config assume the pin definitions here.
 */
GPIO_INT(DETECT_SERVO,    PIN(1, 3),  GPIO_INT_HIGH | GPIO_PULL_DOWN,
	 servo_detect_asserted)
/*
 * Whan TPM_RST_L is asserted, the AP is in reset. Use this for detecting when
 * the AP is off.
 */
GPIO_INT(DETECT_TPM_RST_L_ASSERTED, PIN(1, 4),  GPIO_INT_FALLING,
         tpm_rst_asserted)

/*****************************************************************************/
/* NON STANDARD INTERRUPT GPIOs - handlers defined and configured in board.c */
/*
 * This signal is used as an interrupt for uart bitbang. This is setup manually
 * in board.c instead of using chip/g/gpio.c, so the interrupts can be processed
 * more quickly. This increases the max bitbang programming speed.
 *
 * If this gpio is changed, you must update the information in board.c.
 */
GPIO(EC_TX_CR50_RX,   PIN(1, 11),  GPIO_INPUT)

/*****************************************************************************/
/* GPIOs */
/* Pull this low to interrupt the AP */
GPIO(INT_AP_L,        PIN(0, 0), GPIO_OUT_HIGH)

/* Use these to take over the AP & EC flash (only when AP & EC are off!) */
GPIO(EC_FLASH_SELECT, PIN(0, 1), GPIO_OUT_LOW)
GPIO(AP_FLASH_SELECT, PIN(0, 2), GPIO_OUT_LOW)

/*
 * Pull this low to reset the AP. (We reset the EC with the RBOX.)
 * This is pseudo open drain.
 */
GPIO(SYS_RST_L_OUT,   PIN(0, 4), GPIO_ODR_HIGH)

/*
 * Indicate to EC when CCD is enabled. EC can pull this down too, to tell us if
 * it decided instead.
 * This is pseudo open drain.
 */
GPIO(CCD_MODE_L,      PIN(0, 5), GPIO_ODR_HIGH | GPIO_PULL_UP)

/* Battery present signal is active low */
GPIO(BATT_PRES_L,     PIN(0, 6), GPIO_INPUT)

/* GPIOs used to tristate the SPI bus */
GPIO(SPI_MOSI,        PIN(0, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SPI_CLK,         PIN(0, 8), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SPI_CS_L,        PIN(0, 9), GPIO_INPUT)

/* Used during *chip* factory process. */
GPIO(DIOB4,           PIN(0, 10), GPIO_INPUT | GPIO_PULL_DOWN)

/* GPIOs used for Cr50 strapping options */
GPIO(STRAP_A0,        PIN(1, 12), GPIO_INPUT)
GPIO(STRAP_A1,        PIN(1, 13), GPIO_INPUT)
GPIO(STRAP_B0,        PIN(1, 14), GPIO_INPUT)
GPIO(STRAP_B1,        PIN(1, 15), GPIO_INPUT)

GPIO(UNWEDGE_I2CS_SCL, PIN(1, 5), GPIO_OUT_HIGH)

/*
 * A GPIO to sample the current state of the I2CS SDA line, allowing to detect
 * the 'wedged I2C bus' condition.
 */
GPIO(MONITOR_I2CS_SDA, PIN(1, 6), GPIO_INPUT)

/*
 * If you change the names of EN_PP3300_INA_L, I2C_SCL_INA, or I2C_SDA_INA,
 * you also need to update the usage in closed_source_set1.c
 */
/* Control the load switch powering the INA 3.3V rail */
GPIO(EN_PP3300_INA_L, PIN(0, 11), GPIO_ODR_HIGH)
/* GPIOs used for I2CM pins for INAs */
GPIO(I2C_SCL_INA,     PIN(0, 12), GPIO_INPUT)
GPIO(I2C_SDA_INA,     PIN(0, 13), GPIO_INPUT)

/*
 * Use this to poll the state of the I2CS SDA line. Note that this is not
 * necessary if SPI interface is used to communicate with the AP, if needed,
 * this GPIO could be reclaimed in that case.
 *
 * Actual attachment of this GPIO to the SDA line happens in board.c only when
 * I2CS interface is required. Should this GPIO ever change, the code setting
 * up the pinmux in board.c will have to change as well.
 */
GPIO(I2CS_SDA,        PIN(0, 14), GPIO_INPUT)
/*
 * Fake open drain on EC_TX_CR50_RX_OUT. When asserted, the signal can be used
 * to enable UART programming mode on the EC. The signal needs to fake open
 * drain so it can still be used as the cr50 rx signal when it is deasserted.
 */
GPIO(EC_TX_CR50_RX_OUT, PIN(0, 15), GPIO_ODR_HIGH)

/* Unimplemented signals which we need to emulate for now */
/* TODO(wfrichar): Half the boards don't use this signal. Take it out. */
UNIMPLEMENTED(ENTERING_RW)

/*
 * If we are included by generic GPIO code that doesn't know about the PINMUX
 * macro we need to provide an empty definition so that the invocations don't
 * interfere with other GPIO processing.
 */
#ifndef PINMUX
#define PINMUX(...)
#endif

/* GPIOs - mark outputs as inputs too, to read back from the driven pad */
PINMUX(GPIO(INT_AP_L),        A5, DIO_INPUT)	/* DIOB7 is p_digitial_od */
                                                /* We can't pull it up */
PINMUX(GPIO(EC_FLASH_SELECT), B2, DIO_INPUT)
PINMUX(GPIO(AP_FLASH_SELECT), B3, DIO_INPUT)
PINMUX(GPIO(MONITOR_I2CS_SDA), A1, GPIO_INPUT)

/*
 * Update closed_source_set1.c if pinmux for EN_PP3300_INA_L is changed or
 * removed.
 */
PINMUX(GPIO(EN_PP3300_INA_L), B7, DIO_INPUT)
PINMUX(GPIO(SYS_RST_L_OUT),   M0, DIO_INPUT)
PINMUX(GPIO(CCD_MODE_L),      M1, DIO_INPUT)
PINMUX(GPIO(BATT_PRES_L),     M2, 0)
/*
 * Update closed_source_set1.c if pinmux for I2C_SCL_INA or I2C_SDA_INA is
 * changed or removed.
 */
PINMUX(GPIO(I2C_SCL_INA),     B0, DIO_INPUT)
PINMUX(GPIO(I2C_SDA_INA),     B1, DIO_INPUT)
/* UARTs */
PINMUX(FUNC(UART0_TX),        A0, DIO_OUTPUT)	/* Cr50 console */
PINMUX(FUNC(UART0_RX),        A13, DIO_INPUT | DIO_WAKE_LOW)
/*
 * UART1_TX and UART2_TX are configured in usart.c. They are not set as outputs
 * here in order to avoid interfering with servo. They can be controlled using
 * the 'uart' console command.
 *   UART1_TX = DIOA7	AP console
 *   UART2_TX = DIOB5	EC console
 */
PINMUX(FUNC(UART1_RX),        A3, DIO_INPUT)	/* AP console */
PINMUX(FUNC(UART2_RX),        B6, DIO_INPUT)	/* EC console */
/*
 * Monitor UART RX/TX signals to detect state changes on the EC, AP, and servo.
 *
 * The idle state of the RX signals when the AP or EC are powered on is high.
 * When they are not powered, the signals will remain low. When servo is
 * connected it drives the TX signals high. The servo TX signals are wired
 * to cr50's. Because the two device TX signals are directly wired together,
 * driving the cr50 uart TX at the same time as servo is driving those pins may
 * damage both servo and cr50.
 */
PINMUX(GPIO(DETECT_AP_UART),    A3, DIO_INPUT)
PINMUX(GPIO(DETECT_EC_UART),    B6, DIO_INPUT)
PINMUX(GPIO(EC_TX_CR50_RX),     B6, DIO_INPUT)
PINMUX(GPIO(EC_TX_CR50_RX_OUT), B6, DIO_INPUT)
PINMUX(GPIO(DETECT_SERVO),      B5, DIO_INPUT)

/*
 * I2CS pins are bi-directional and would be configured here as shown. However,
 * A1 is also used as a strapping option GPIO input which is configured
 * above. If a board is configured (via the strapping pins) to support the I2CS
 * interface, then the connection of A1 and A9 to/from the I2C0_SDA and I2C0_SCL
 * lines is done in the function i2cs_set_pinmux() which lives in board.c.
 *
 * PINMUX(FUNC(I2C0_SCL),        A9, DIO_INPUT)
 * PINMUX(FUNC(I2C0_SDA),        A1, DIO_INPUT)
*/

/*
 * Both SPI master and slave buses are wired directly to specific pads
 *
 * If CONFIG_SPS is defined, these pads are used:
 *   DIOA2  = SPS_MOSI  (input)
 *   DIOA6  = SPS_CLK   (input)
 *   DIOA10 = SPS_MISO  (output)
 *   DIOA12 = SPS_CS_L  (input)
 * The digital inputs are enabled in sps.c
 *
 * If CONFIG_SPI_MASTER is defined, these pads are used:
 *   DIOA4  = SPI_MOSI  (output)
 *   DIOA8  = SPI_CLK   (output)
 *   DIOA11 = SPI_MISO  (input)
 *   DIOA14 = SPI_CS_L  (output)
 * The pads are only connected to the peripheral outputs when SPI is enabled to
 * avoid interfering with other things on the board.
 * Note: Double-check to be sure these are configured in spi_master.c
 */
PINMUX(GPIO(SPI_MOSI),           A4, DIO_OUTPUT)
PINMUX(GPIO(SPI_CLK),            A8, DIO_OUTPUT)
PINMUX(GPIO(SPI_CS_L),          A14, DIO_OUTPUT)

PINMUX(GPIO(DIOB4),              B4, DIO_INPUT)


#undef PINMUX
