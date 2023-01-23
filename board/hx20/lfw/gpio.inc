/* -*- mode:c -*-
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Minimal set of GPIOs needed for LFW loader
 */

/*
 * MEC1701H GPIO_0055/PWM2/SHD_CS0#/RSMRST#
 * MEC1701H QMSPI controller drives chip select, must be
 * configured to alternative function. See below.
 * GPIO_SHD_CS0 is used in board level spi_devices[] table
 */
GPIO(QMSPI_CS0,	PIN(055),  GPIO_ODR_HIGH)

/* We need to take control of this away from VCI */
GPIO(EC_ON, PIN(0250), GPIO_OUT_HIGH) /* keep +3VL_EC to power on */

/* Alternate functions GPIO definition */

/*
 * UART
 * GPIO_0104(UART0_TX) Func1
 * GPIO_0105(UART0_RX) Func1
 * Bank 2 bits[4:5]
*/
ALTERNATE(PIN_MASK(2, 0x30), 1, MODULE_UART, 0)

/* SPI pins */
/*
 * MEC1701H SHD SPI is connected to QMSPI controller.
 * QMSPI drives chip select. SHD_CS0#(GPIO_0055) must be set
 * to alternate function 2 and GPIO_ODR_HIGH.
 */
/* GPIO_0055 Function 2, Bank 1 bit[13] */
ALTERNATE(PIN_MASK(1, 0x2000), 2, MODULE_SPI_FLASH, GPIO_ODR_HIGH)
/* SHD_CLK - GPIO_0056 Function 2, Bank 1 bit[14] */
ALTERNATE(PIN_MASK(1, 0x4000), 2, MODULE_SPI_FLASH, 0)
/* MOSI(SHD_IO0) - GPIO_0223 Function 1, Bank 4 bit[19] */
/* MISO(SHD_IO1) - GPIO_0224 Function 2, Bank 4 bit[20] */
/* nWP(SHD_IO2) - GPIO_0227 Function 1, Bank 4 bit[23] */
ALTERNATE(PIN_MASK(4, 0x880000), 1, MODULE_SPI_FLASH, 0)
ALTERNATE(PIN_MASK(4, 0x100000), 2, MODULE_SPI_FLASH, 0)
/* nHOLD(SHD_IO3) - GPIO_0016 Function 2, Bank 0 bit[14] */
ALTERNATE(PIN_MASK(0, 0x4000), 2, MODULE_SPI_FLASH, 0)
