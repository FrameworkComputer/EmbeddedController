/* -*- mode:c -*-
 * Copyright (c) 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Declare symbolic names for all the GPIOs that we care about.
 * Note: Those with interrupt handlers must be declared first. */

/* Pull this low to interrupt the AP */
GPIO(INT_AP_L,        PIN(0, 0), GPIO_INPUT)

/* Use these to take over the AP & EC flash (only when AP & EC are off!) */
GPIO(EC_FLASH_SELECT, PIN(0, 1), GPIO_OUT_LOW)
GPIO(AP_FLASH_SELECT, PIN(0, 2), GPIO_OUT_LOW)

/* As an input this mirrors EC_WP_L (which is controlled by RBOX, not a GPIO).
 * As an output it can override EC_WP_L, although why would we? */
GPIO(AP_WP_L,         PIN(0, 3), GPIO_INPUT)

/* Drive high to reset the EC & AP */
GPIO(SYS_RST,         PIN(0, 4), GPIO_INPUT)

/* Indicate to EC when CCD is enabled */
GPIO(CCD_MODE,        PIN(0, 5), GPIO_INPUT)

/* Battery has a 10K pulldown on its side. We provide the pullup. */
GPIO(BATT_PRES,       PIN(0, 6), GPIO_INPUT)

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
PINMUX(GPIO(AP_WP_L),         M3, 0)
PINMUX(GPIO(SYS_RST),         M0, DIO_INPUT)
PINMUX(GPIO(CCD_MODE),        M1, DIO_INPUT)
PINMUX(GPIO(BATT_PRES),       M2, 0)

/* UARTs */
PINMUX(FUNC(UART0_TX),        A0, DIO_OUTPUT)	/* Cr50 console */
PINMUX(FUNC(UART0_RX),        A1, DIO_INPUT | DIO_WAKE_LOW)
PINMUX(FUNC(UART1_TX),        A3, DIO_INPUT)	/* AP console */
PINMUX(FUNC(UART1_RX),        A7, DIO_INPUT)
PINMUX(FUNC(UART2_TX),        B5, DIO_INPUT)	/* EC console */
PINMUX(FUNC(UART2_RX),        B6, DIO_INPUT)

/* I2C pins are bi-directional */
PINMUX(FUNC(I2C0_SCL),        B0, DIO_INPUT)
PINMUX(FUNC(I2C0_SDA),        B1, DIO_INPUT)

/* Both SPI master and slave buses are wired directly to specific pads
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
 * Note: Double-check to be sure these are configured in spi_master.c
 */

#undef PINMUX
