/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Interrupts */
GPIO_INT(FP_MCU_INT_L,         PIN(B, 0), GPIO_INT_RISING, fps_event)
GPIO_INT(HOST_MCU_WP_OD,       PIN(A, 4), GPIO_INT_BOTH, switch_interrupt)

/* SHI CS Ready, Low Active. */
GPIO_INT(SPI_HOST_CS_MCU_ODL,  PIN(5, 3), GPIO_INT_FALLING,shi_cs_event)
GPIO_INT(SLP_L,                PIN(A, 0), GPIO_INT_BOTH, slp_event)

/* Inputs */
GPIO(TRANSPORT_SEL,            PIN(4, 3), GPIO_INPUT)
GPIO(FP_SENSOR_SEL,            PIN(4, 4), GPIO_INPUT)
/*
 * SPI host interface - enable PDs by default. These will be made functional
 * by the SHI driver when the AP powers up, and restored back to GPIO when
 * the AP powers down.
 */
GPIO(SPI_HOST_DO_MCU_DI,       PIN(4, 6), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SPI_HOST_DI_MCU_DO,       PIN(4, 7), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(SPI_HOST_CLK_MCU,         PIN(5, 5), GPIO_INPUT | GPIO_PULL_DOWN)
GPIO(MCU_PLATFORM_INT_L,       PIN(A, 7),  GPIO_ODR_HIGH)

/* Outputs */
GPIO(FP_RST_ODL,               PIN(9, 6),  GPIO_OUT_HIGH)
GPIO(SPI_MCU_CS_FP_L,          PIN(A, 6),  GPIO_OUT_HIGH)
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

/* SPIP - to fingerprint sensor */
/* SPIP_MOSI/SPIP_SCLK GPIOA3/A1 */
ALTERNATE(PIN_MASK(A, 0x0A), 1, MODULE_SPI, 0)
/* SPIP_MISO GPIO95    */
ALTERNATE(PIN_MASK(9, 0x20), 1, MODULE_SPI, 0)

UNIMPLEMENTED(ENTERING_RW)
