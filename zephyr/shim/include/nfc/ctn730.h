/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/nfc/ctn730.h"
#include "peripheral_charger.h"

#include <zephyr/devicetree.h>

#define CTN730_PCHG_COMPAT nxp_ctn730

extern struct pchg_drv ctn730_drv;

#define NFC_CHIP_CTN730(id) \
	{								\
		.cfg = &(struct pchg_config) {                    \
			.i2c_port = I2C_PORT_BY_DEV(id),           	\
			.drv = &ctn730_drv,                             \
			.irq_pin = GPIO_WPC_IRQ, \
			.full_percent = DT_PROP(id, full_percent),  \
			.block_size = DT_PROP(id, block_size),      \
			.rf_charge_msec = DT_PROP(id, rf_charge_msec),      \
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
