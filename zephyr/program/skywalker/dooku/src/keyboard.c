/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "gpio_it8xxx2.h"
#include "keyboard_scan.h"

/*
 * Map keyboard connector pins to EC GPIO pins for factory test.
 * Pins mapped to {-1, -1} are skipped.
 * The connector has 30 pins total, and there is no pin 0.
 */
const int keyboard_factory_scan_pins[][2] = {
	{ -1, -1 },	  { GPIO_KSOH, 4 }, { GPIO_KSOH, 0 }, { GPIO_KSOH, 1 },
	{ GPIO_KSOH, 3 }, { GPIO_KSOH, 2 }, { -1, -1 },	      { -1, -1 },
	{ GPIO_KSOL, 5 }, { GPIO_KSOL, 6 }, { -1, -1 },	      { GPIO_KSOL, 3 },
	{ GPIO_KSOL, 2 }, { GPIO_KSI, 0 },  { GPIO_KSOL, 1 }, { GPIO_KSOL, 4 },
	{ GPIO_KSI, 3 },  { GPIO_KSI, 2 },  { GPIO_KSOL, 0 }, { GPIO_KSI, 5 },
	{ GPIO_KSI, 4 },  { GPIO_KSOL, 7 }, { GPIO_KSI, 6 },  { GPIO_KSI, 7 },
	{ GPIO_KSI, 1 },  { -1, -1 },	    { GPIO_KSOH, 5 }, { -1, -1 },
	{ GPIO_KSOH, 6 }, { -1, -1 },	    { -1, -1 },
};
const int keyboard_factory_scan_pins_used =
	ARRAY_SIZE(keyboard_factory_scan_pins);
