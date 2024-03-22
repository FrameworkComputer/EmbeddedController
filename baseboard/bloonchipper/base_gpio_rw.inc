/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SECTION_IS_RW
#error "This file should only be included in RW."
#endif

/* Interrupts */
GPIO_INT(FPS_INT,         PIN(A, 0), GPIO_INT_RISING, fps_event)

/* Inputs */
GPIO(FP_SENSOR_SEL,       PIN(B, 0), GPIO_INPUT)

/* Outputs */
GPIO(FP_RST_ODL,          PIN(B,10),  GPIO_OUT_HIGH)
GPIO(SPI2_NSS,            PIN(B,12),  GPIO_OUT_HIGH)
GPIO(USER_PRES_L,         PIN(B, 9),  GPIO_ODR_HIGH)

/*
 * SPI2 master to sensor: PB13/14/15 (CLK/MISO/MOSI)
 * Note that we're not configuring NSS (PB12) here because we have already
 * configured it as a GPIO above and the SPI_MASTER module expects to use it
 * in software NSS management mode, not hardware management mode.
 */
ALTERNATE(PIN_MASK(B, 0xE000), GPIO_ALT_SPI, MODULE_SPI_CONTROLLER, GPIO_PULL_DOWN)
