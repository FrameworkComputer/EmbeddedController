/* -*- mode:c -*-
 *
 * Copyright 2023 The ChromiumOS Authors
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
