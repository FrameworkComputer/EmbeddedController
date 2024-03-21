/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SECTION_IS_RW
#error "This file should only be included in RW."
#endif

/* Interrupts */
GPIO_INT(FP_MCU_INT_L,         PIN(B, 0), GPIO_INT_RISING, fps_event)

/* Inputs */
GPIO(FP_SENSOR_SEL,            PIN(4, 4), GPIO_INPUT)

/* Outputs */
GPIO(FP_RST_ODL,               PIN(9, 6),  GPIO_OUT_HIGH)
GPIO(SPI_MCU_CS_FP_L,          PIN(A, 6),  GPIO_OUT_HIGH)

/* SPIP - to fingerprint sensor */
/* SPIP_MOSI/SPIP_SCLK GPIOA3/A1 */
ALTERNATE(PIN_MASK(A, 0x0A), 1, MODULE_SPI, 0)
/* SPIP_MISO GPIO95    */
ALTERNATE(PIN_MASK(9, 0x20), 1, MODULE_SPI, 0)