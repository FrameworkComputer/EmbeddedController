/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"
#include "peripheral_charger.h"
#include "wpc/cps8601.h"
#include "wpc/scp8200.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_PLATFORM_EC_PERIPHERAL_CHARGER

#define WPC_CHIP_ELE_CPS8200(id) WPC_CHIP_CPS8200(id),
#define WPC_CHIP_ELE_CPS8601(id) WPC_CHIP_CPS8601(id),

struct pchg pchgs[] = { DT_FOREACH_STATUS_OKAY(CPS8200_PCHG_COMPAT,
					       WPC_CHIP_ELE_CPS8200)
				DT_FOREACH_STATUS_OKAY(CPS8601_PCHG_COMPAT,
						       WPC_CHIP_ELE_CPS8601) };

unsigned int pchg_count = ARRAY_SIZE(pchgs);

int board_get_pchg_count(void)
{
	return pchg_count;
}
#endif
