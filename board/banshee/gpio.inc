/* -*- mode:c -*-
 *
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define MODULE_KB		MODULE_KEYBOARD_SCAN

/*
 * Generated-gpio.inc is produced using a Brya specific tool that
 * parses the GPIO definitions derived from the board schematics and
 * EC pinout descriptions derived form the chip datasheets to generate
 * the Chrome EC GPIO pinout definitions. Due to the confidential
 * nature of schematics and datasheets, they are not provided here.
 *
 * Variants that do not auto-generate their GPIO definitions should
 * combine the Brya gpio.inc and generated-gpio.inc into their
 * gpio.inc and customize as appropriate.
 */

#include "generated-gpio.inc"

/*
 * The NPCX keyboard driver does not use named GPIOs to access
 * keyboard scan pins, so we do not list them in *gpio.inc. However, when
 * KEYBOARD_COL2_INVERTED is defined, this name is required.
 */
GPIO(EC_KSO_02_INV,               PIN(1, 7), GPIO_OUT_LOW)

/*
 * GPIOE1 is an ALT function ADC INPUT on board ID 2 and a GPIO INPUT on
 * board ID 1. This declaration gives us a signal name to use on board
 * ID 1.
 */
GPIO(ID_1_EC_BATT_PRES_ODL,       PIN(E, 1), GPIO_INPUT)

/*
 * GPIO34 is an INPUT on board ID 2 and ODR_LOW on board ID 1.
 *
 * Since this pin is pulled up to 3.3V through a 30.9K ohm resistor on
 * board ID 2, we will leak about 0.3mW until the pin is put in ALT mode
 * when MODULE_ADC configuration runs. Initializing the pin to ODR_LOW
 * gives us full control on both boards.
 */
GPIO(ID_1_USB_C0_C2_TCPC_RST_ODL, PIN(3, 4), GPIO_ODR_LOW)

/* Board ID 1 IO expander configuration */

IOEX(ID_1_USB_C0_RT_RST_ODL, EXPIN(IOEX_ID_1_C0_NCT38XX, 0, 2), GPIO_ODR_LOW)
/* GPIO03_P1 to PU */
IOEX(ID_1_USB_C0_FRS_EN,     EXPIN(IOEX_ID_1_C0_NCT38XX, 0, 4), GPIO_LOW)
IOEX(ID_1_USB_C0_OC_ODL,     EXPIN(IOEX_ID_1_C0_NCT38XX, 0, 6), GPIO_ODR_HIGH)
/* GPIO07_P1 to PU */

IOEX(ID_1_USB_C2_RT_RST_ODL, EXPIN(IOEX_ID_1_C2_NCT38XX, 0, 2), GPIO_ODR_LOW)
/* GPIO03_P2 to PU */
IOEX(ID_1_USB_C2_FRS_EN,     EXPIN(IOEX_ID_1_C2_NCT38XX, 0, 4), GPIO_LOW)
IOEX(ID_1_USB_C1_OC_ODL,     EXPIN(IOEX_ID_1_C2_NCT38XX, 0, 6), GPIO_ODR_HIGH)
IOEX(ID_1_USB_C2_OC_ODL,     EXPIN(IOEX_ID_1_C2_NCT38XX, 0, 7), GPIO_ODR_HIGH)

/* Board ID 2 IO expander configuration */

/* GPIO02_P2 to PU */
/* GPIO03_P2 to PU */
IOEX(USB_C0_OC_ODL,          EXPIN(IOEX_C0_NCT38XX, 0, 4), GPIO_ODR_HIGH)
IOEX(USB_C0_FRS_EN,          EXPIN(IOEX_C0_NCT38XX, 0, 6), GPIO_LOW)
IOEX(USB_C0_RT_RST_ODL,      EXPIN(IOEX_C0_NCT38XX, 0, 7), GPIO_ODR_LOW)

IOEX(USB_C2_RT_RST_ODL,      EXPIN(IOEX_C2_NCT38XX, 0, 2), GPIO_ODR_LOW)
IOEX(USB_C1_OC_ODL,          EXPIN(IOEX_C2_NCT38XX, 0, 3), GPIO_ODR_HIGH)
IOEX(USB_C2_OC_ODL,          EXPIN(IOEX_C2_NCT38XX, 0, 4), GPIO_ODR_HIGH)
IOEX(USB_C2_FRS_EN,          EXPIN(IOEX_C2_NCT38XX, 0, 6), GPIO_LOW)
/* GPIO07_P2 to PU */
