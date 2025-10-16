/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/wpc/cps8100.h"
#include "peripheral_charger.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPS8601_PCHG_COMPAT convenientpower_cps8601

#define WPC_CHIP_CPS8601(id) \
	{								\
		.cfg = &(struct pchg_config) {                    \
			.i2c_port = I2C_PORT_BY_DEV(id),           	\
			.drv = &cps8601_drv,                             \
			.irq_pin = GPIO_WPC_IRQ, \
			.full_percent = DT_PROP(id, full_percent),  \
			.block_size = DT_PROP(id, block_size),      \
			.flags = PCHG_CFG_FW_UPDATE_SYNC,           \
		},\
		.policy = {\
			[PCHG_CHIPSET_STATE_ON] = &pchg_policy_on,\
			[PCHG_CHIPSET_STATE_SUSPEND] = &pchg_policy_suspend,\
		},\
		.events = QUEUE_NULL(PCHG_EVENT_QUEUE_SIZE, enum pchg_event),\
	}
#ifdef __cplusplus
}
#endif
