/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SECTION_IS_RW
#error "This file should only be included in RW."
#endif

/* Interrupts */
GPIO_INT(FPS_INT,         PIN(A, 0), GPIO_INT_RISING, fps_event)

#ifdef APPLY_RESET_LOOP_FIX
GPIO_INT(WP,              PIN(B, 7), GPIO_INT_BOTH, wp_event)
#endif

/* Inputs */
GPIO_INT(SLP_ALT_DEV_L,   PIN(D,14), GPIO_INT_BOTH, slp_event)
/*
 * The S4 and SUS sleep lines are unused in code, but are maintained in this
 * gpio list to ensure that they are not repurposed. This is because these
 * inputs are driven on nocturne.
 */
GPIO(PCH_SLP_S4_L,        PIN(D, 8), GPIO_INPUT)
GPIO(PCH_SLP_SUS_L,       PIN(D, 3), GPIO_INPUT)
/* TODO(b/178808871): Only enable pull up when doing detection. */
GPIO(FP_SPI_SEL,          PIN(E, 1), GPIO_INPUT | GPIO_PULL_UP)

/* Outputs */
GPIO(DIVIDER_HIGHSIDE,    PIN(B, 8),  GPIO_OUT_LOW)
GPIO(FP_RST_ODL,          PIN(E, 0),  GPIO_OUT_HIGH)
GPIO(SPI4_NSS,            PIN(E, 4),  GPIO_OUT_HIGH)
GPIO(SPI4_ALT_NSS,        PIN(E, 11), GPIO_OUT_HIGH)
GPIO(USER_PRES_L,         PIN(C, 5),  GPIO_ODR_HIGH)

/* Default SPI4 master to sensor: PE2/5/6 (CLK/MISO/MOSI) */
ALTERNATE(PIN_MASK(E, 0x0064), GPIO_ALT_SPI, MODULE_SPI_CONTROLLER,  0)

/* See board_rw.c for the alternate configuration that we use on
 * development boards, which overrides the UNUSED values below.
 */
UNUSED(PIN(E, 12))  /* Alternate SPI4 master to sensor CLK */
UNUSED(PIN(E, 13))  /* Alternate SPI4 master to sensor MISO */
UNUSED(PIN(E, 14))  /* Alternate SPI4 master to sensor MOSI */
