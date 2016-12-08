/* -*- mode:c -*-
 * Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/*
 * We can assert SYS_RST_L but so can the EC, so we need react if it's pulled
 * low. The ARM core can't trigger an interrupt if it's driving it as an output
 * so we attach two internal GPIOs to the same pad.
 */
GPIO_INT(SYS_RST_L_IN,    PIN(1, 0),  GPIO_INT_FALLING, sys_rst_asserted)
GPIO_INT(PLT_RST_L,       PIN(1, 10), GPIO_INT_FALLING, sys_rst_asserted)
GPIO_INT(DETECT_AP,       PIN(1, 1),  GPIO_INT_HIGH, device_state_on)
GPIO_INT(DETECT_EC,       PIN(1, 2),  GPIO_INT_HIGH, device_state_on)
GPIO_INT(DETECT_SERVO,    PIN(1, 3),  GPIO_INT_HIGH | GPIO_PULL_DOWN,
	 device_state_on)

/* Pull this low to interrupt the AP */
GPIO(INT_AP_L,        PIN(0, 0), GPIO_OUT_HIGH)

/* Use these to take over the AP & EC flash (only when AP & EC are off!) */
GPIO(EC_FLASH_SELECT, PIN(0, 1), GPIO_OUT_LOW)
GPIO(AP_FLASH_SELECT, PIN(0, 2), GPIO_OUT_LOW)

/* Pull this low to reset the AP. (We reset the EC with the RBOX.) */
GPIO(SYS_RST_L_OUT,   PIN(0, 4), GPIO_INPUT)

/* Indicate to EC when CCD is enabled. EC can pull this down too, to tell us if
 * it decided instead. The pullup is on the EC's side. */
GPIO(CCD_MODE_L,      PIN(0, 5), GPIO_ODR_HIGH)

/* Battery present signal is active low */
GPIO(BATT_PRES_L,     PIN(0, 6), GPIO_INPUT)

/* GPIOs used to tristate the SPI bus */
GPIO(SPI_MOSI,        PIN(0, 7), GPIO_INPUT)
GPIO(SPI_CLK,         PIN(0, 8), GPIO_INPUT)
GPIO(SPI_CS_L,        PIN(0, 9), GPIO_INPUT)

/* GPIOs used for Cr50 strapping options */
GPIO(STRAP0,        PIN(0, 10), GPIO_INPUT)

/* Control the load switch powering the INA 3.3V rail */
GPIO(EN_PP3300_INA_L, PIN(0, 11), GPIO_ODR_HIGH)
/* GPIOs used for I2CM pins for INAs */
GPIO(I2C_SCL_INA,     PIN(0, 12), GPIO_INPUT)
GPIO(I2C_SDA_INA,     PIN(0, 13), GPIO_INPUT)

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
PINMUX(GPIO(EN_PP3300_INA_L), B7, DIO_INPUT)
PINMUX(GPIO(SYS_RST_L_IN),    M0, DIO_WAKE_FALLING)
PINMUX(GPIO(SYS_RST_L_OUT),   M0, DIO_INPUT)
PINMUX(GPIO(CCD_MODE_L),      M1, DIO_INPUT)
PINMUX(GPIO(BATT_PRES_L),     M2, 0)
PINMUX(GPIO(STRAP0),          A1, DIO_INPUT)
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
PINMUX(GPIO(DETECT_AP),       A3, DIO_INPUT)
PINMUX(GPIO(DETECT_EC),       B6, DIO_INPUT)
PINMUX(GPIO(DETECT_SERVO),    B5, DIO_INPUT)

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
#undef PINMUX
