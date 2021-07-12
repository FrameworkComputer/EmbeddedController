/* -*- mode:c -*-
 *
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common GPIOs needed for LFW loader and main process FW
 */

/* SPI
 * External SPI chip select must be open drain and driven high or
 * internal SPI chip select must be push-pull and driven high before
 * SPI controller configuration.
 * QMSPI external Shared CS0# is GPIO_0055
 * QMSPI internal CS0# is GPIO_0116
 */
#if defined(CHIP_VARIANT_MEC1727SZ)
GPIO(QMSPI_CS0, PIN(0116), GPIO_PULL_UP | GPIO_HIGH)
#else
GPIO(QMSPI_CS0, PIN(055), GPIO_ODR_HIGH)
#endif

/* Boot-ROM loads from external or internal SPI flash.
 * There are two external ports: shared(default) and private.
 * NOTE: QMSPI Shared SPI Port pins are on VTR2
 * SHD_CS0# = GPIO 0055 Func 2 bank 1 b[13]
 * SHD_CLK  = GPIO 0056 Func 2 bank 1 b[14]
 * SHD_IO0  = GPIO 0223 Func 1 bank 4 b[19]
 * SHD_IO1  = GPIO 0224 Func 2 bank 4 b[20]
 * Not using IO2 and IO2 as data
 * SHD_IO2  = GPIO 0227 Func 1 bank 4 b[23]
 * SHD_IO3  = GPIO 0016 Func 2 bank 0 b[14]
 * MEC1727 variants load from internal 512KB SPI flash(internal only pins)
 * INT_CS# = GPIO 0116 Func 1 bank 2 14
 * INT_SCK = GPIO 0117 Func 1 bank 2 15
 * INT_IO0 = GPIO 0074 Func 1 bank 1 28
 * INT_IO1 = GPIO 0075 Func 1 bank 1 29
 * INT_WP# = GPIO 0076 Func 0 for WP# control
 * Internal flash HOLD# connected to VTR1 rail.
 */
#if defined(CHIP_VARIANT_MEC1727SZ)
/* MEC1727 variants have internal SPI flash on internal only pins */
ALTERNATE(PIN_MASK(2, 0x4000), 1, MODULE_SPI_FLASH, GPIO_PULL_UP)
ALTERNATE(PIN_MASK(2, 0x8000), 1, MODULE_SPI_FLASH, 0)
ALTERNATE(PIN_MASK(1, 0x30000000), 1, MODULE_SPI_FLASH, 0)
#else
/* external SPI flash on QMSPI SHD_xx pins */
ALTERNATE(PIN_MASK(1, 0x2000), 2, MODULE_SPI_FLASH, GPIO_ODR_HIGH)
ALTERNATE(PIN_MASK(1, 0x4000), 2, MODULE_SPI_FLASH, 0)
ALTERNATE(PIN_MASK(4, 0x080000), 1, MODULE_SPI_FLASH, 0)
ALTERNATE(PIN_MASK(4, 0x100000), 2, MODULE_SPI_FLASH, 0)
#endif

/* UART
 * Per CONFIG_UART_CONSOLE and chip to configure UART pins
 */
#if CONFIG_UART_CONSOLE == 0
/* select UART0 */
/* MEC170X, MEC152X and MEC172X support same UART0 pins and ALT function */
/*
 * UART0
 * GPIO_0105 Func 1 = UART_RX
 * GPIO_0104 Func 1 = UART_TX
 * Bank 2 bits[5:4]
 */
ALTERNATE(PIN_MASK(2, 0x30), 1, MODULE_UART, 0)

#elif CONFIG_UART_CONSOLE == 1
/* select UART1 */
/* MEC170X, MEC152X and MEC172X support same UART1 pins
 * but ALT function 2 on MEC170X, function 1 on others
 */
#if defined(CHIP_FAMILY_MEC170X)
/*
 * UART1
 * GPIO_0171 Func 2 = UART_RX
 * GPIO_0170 Func 2 = UART_TX
 * Bank 3 bits[25:24]
 */
ALTERNATE(PIN_MASK(3, 0x03000000), 2, MODULE_UART, 0)

#else
/*
 * UART1
 * GPIO_0171 Func 1 = UART_RX
 * GPIO_0170 Func 1 = UART_TX
 * Bank 3 bits[25:24]
 */
ALTERNATE(PIN_MASK(3, 0x03000000), 1, MODULE_UART, 0)

#endif /* defined(CHIP_FAMILY_MEC170X) */

#else
/* select UART2 */
/* only MEC152X supports UART2 pins */
#if defined(CHIP_FAMILY_MEC152X)
/*
 * UART2
 * GPIO_0145 Func 2 = UART_RX
 * GPIO_0146 Func 2 = UART_TX
 * Bank 3 bits[6:5]
 */
ALTERNATE(PIN_MASK(3, 0x60), 2, MODULE_UART, 0)

#endif /* defined(CHIP_FAMILY_MEC152X) */

#endif /* CONFIG_UART_CONSOLE == 0 */
