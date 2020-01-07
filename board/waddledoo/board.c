/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "common.h"
#include "compile_time_macros.h"
#include "extpower.h"
#include "gpio.h"
#include "i2c.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"

#include "gpio_list.h"

int extpower_is_present(void)
{
	/*
	 * TODO(b:146651593) We can likely use the charger IC to determine VBUS
	 * presence.
	 */
	return 1;
}
