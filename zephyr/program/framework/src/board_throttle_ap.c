/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "gpio/gpio_int.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ##args)

#if DT_NODE_EXISTS(DT_ALIAS(gpio_throttle_ap))
void chipset_throttle_cpu(int throttle)
{
	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		/**
		 * The EC_PROCHOT# pin power rail is not 3.3V in some project.
		 * We can't use the gpioget command to check the PROCHOT status.
		 * Therefore, add the debug message here for debugging.
		 */
		CPRINTS("EC %s the AP PROCHOT", throttle ? "asserts" : "de-asserts");
		gpio_pin_set_dt(GPIO_DT_FROM_ALIAS(gpio_throttle_ap), !throttle);
	}
}
#else
#error "CONFIG_PLATFORM_EC_CHIPSET_CAN_THROTTLE is enabled but the gpio-throttle-ap devicetree alias is missing"
#endif
