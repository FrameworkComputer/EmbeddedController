/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
/* HOST_MCU_WP_OD in the schematic. */
GPIO_INT(WP,                   PIN(A, 4), GPIO_INT_BOTH, switch_interrupt)

/* SHI CS Ready, Low Active. SPI_HOST_CS_MCU_ODL in the schematic. */
GPIO_INT(SHI_CS_L,  PIN(5, 3), GPIO_INT_FALLING,shi_cs_event)
GPIO_INT(SLP_L,                PIN(A, 0), GPIO_INT_BOTH, slp_event)

/* Inputs */
GPIO(TRANSPORT_SEL,            PIN(4, 3), GPIO_INPUT)
/*
 * SPI host interface - enable PDs by default. These will be made functional
 * by the SHI driver when the AP powers up, and restored back to GPIO when
 * the AP powers down.
 */
GPIO(SPI_HOST_DO_MCU_DI,       PIN(4, 6), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SPI_HOST_DI_MCU_DO,       PIN(4, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SPI_HOST_CLK_MCU,         PIN(5, 5), GPIO_INPUT | GPIO_PULL_DOWN)
/* MCU_PLATFORM_INT_L in the schematic. */
GPIO(EC_INT_L,                 PIN(A, 7),  GPIO_ODR_HIGH)

/* Outputs */
GPIO(DIVIDER_HIGHSIDE,         PIN(9, 3),  GPIO_OUT_LOW)

/* UART1 Tx/Rx */
#if NPCX_UART_MODULE2
ALTERNATE(PIN_MASK(6, 0x30), 1, MODULE_UART, 0) /* CR_SIN/SOUT  GPIO64/65 */
#else
ALTERNATE(PIN_MASK(1, 0x03), 1, MODULE_UART, 0) /* CR_SIN/SOUT  GPIO10/11 */
#endif

/* UART2 Tx/Rx */
ALTERNATE(PIN_MASK(7, 0x20), 1, MODULE_UART, 0) /* CR_SIN2  GPIO75 */
ALTERNATE(PIN_MASK(8, 0x40), 1, MODULE_UART, 0) /* CR_SOUT2 GPIO86 */

UNIMPLEMENTED(ENTERING_RW)

#ifdef SECTION_IS_RW
#include "base_gpio_rw.inc"
#endif
