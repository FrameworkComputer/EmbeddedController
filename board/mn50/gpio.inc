/* -*- mode:c -*-
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
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


/* Use these to reset/flash the DUT haven */
GPIO(DUT_PWR_EN,      PIN(0,  2), GPIO_OUT_LOW)     /* DIOB5 */
GPIO(DUT_PWRGOOD,     PIN(0,  3), GPIO_INPUT)       /* DIOB7 */
/* These GPIOS are switched between input/output by socket enable. */
GPIO(DUT_BOOT_CFG,    PIN(0,  0), GPIO_OUT_LOW)     /* DIOB2 */
GPIO(DUT_RST_L,       PIN(0,  1), GPIO_OUT_LOW)     /* DIOB3 */

GPIO(LED_B_L,         PIN(0,  4), GPIO_ODR_HIGH)    /* DIOA9 */
GPIO(LED_R_L,         PIN(0,  5), GPIO_ODR_HIGH)    /* DIOA13 */
GPIO(LED_G_L,         PIN(0,  6), GPIO_ODR_HIGH)    /* DIOA14 */
GPIO(LED_L,           PIN(0, 11), GPIO_ODR_HIGH)    /* DIOB6 */

/* GPIOs used to tristate the SPI bus */
GPIO(SPI_MOSI,        PIN(0,  7), GPIO_INPUT)       /* DIOA4 */
GPIO(SPI_CLK,         PIN(0,  8), GPIO_INPUT)       /* DIOA8 */
GPIO(SPI_CS_L,        PIN(0,  9), GPIO_INPUT)       /* DIOA14 */
GPIO(SPI_CS_ALT_L,    PIN(0, 10), GPIO_INPUT)       /* DIOA5 */

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
PINMUX(GPIO(DUT_BOOT_CFG),    B2, DIO_INPUT)
PINMUX(GPIO(DUT_RST_L),       B3, DIO_INPUT)
PINMUX(GPIO(DUT_PWR_EN),      B5, DIO_INPUT)
PINMUX(GPIO(DUT_PWRGOOD),     B7, DIO_INPUT)

PINMUX(GPIO(LED_B_L),         A9,  DIO_INPUT)
PINMUX(GPIO(LED_R_L),         A13, DIO_INPUT)
PINMUX(GPIO(LED_G_L),         A14, DIO_INPUT)
PINMUX(GPIO(LED_L),           B6,  DIO_INPUT)

/* UARTs */
PINMUX(FUNC(UART0_TX),        A0, DIO_OUTPUT)	/* Cr50 console */
PINMUX(FUNC(UART0_RX),        A1, DIO_INPUT | DIO_WAKE_LOW)

/*
 * UART1_TX will be enabled when the socket power is enabled,
 * to prevent backpowering.
 *
 * PINMUX(FUNC(UART1_TX),        A7, DIO_OUTPUT)
 */

/* DUT console */
PINMUX(FUNC(UART1_RX),        A3, DIO_INPUT)

/* I2C setup */
PINMUX(FUNC(I2C0_SCL),        B0, DIO_INPUT | DIO_OUTPUT)
PINMUX(FUNC(I2C0_SDA),        B1, DIO_INPUT | DIO_OUTPUT)

/*
 * Both SPI master and slave buses are wired directly to specific pads
 *
 * If CONFIG_SPI_MASTER is defined, these pads are used:
 *   DIOA4  = SPI_MOSI  (output)
 *   DIOA8  = SPI_CLK   (output)
 *   DIOA11 = SPI_MISO  (input)
 *   DIOA14 = SPI_CS_L  (output) - mn50 doesn't use HS CS implementation.
 * The pads are only connected to the peripheral outputs when SPI is enabled to
 * avoid interfering with other things on the board.
 * Note: Double-check to be sure these are configured in spi_master.c
 */
PINMUX(GPIO(SPI_MOSI),           A4, DIO_OUTPUT)
PINMUX(GPIO(SPI_CLK),            A8, DIO_OUTPUT)
PINMUX(GPIO(SPI_CS_ALT_L),       A5, DIO_OUTPUT)

#undef PINMUX
