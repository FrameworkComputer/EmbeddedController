/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT pericom_pi3usb9201

#include <zephyr/devicetree.h>
#include "bc12/pi3usb9201_public.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "usbc/utils.h"
#include "i2c/i2c.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) > 0,
	     "No compatible BC1.2 instance found");

#define USBC_PORT_BC12(usbc_id, bc12_id)                \
	[USBC_PORT_NEW(usbc_id)] = {                    \
		.i2c_port = I2C_PORT_BY_DEV(bc12_id),   \
		.i2c_addr_flags = DT_REG_ADDR(bc12_id), \
	},

#define PI3SUSB9201_CHECK(usbc_id, bc12_id)                          \
	COND_CODE_1(DT_NODE_HAS_COMPAT(bc12_id, pericom_pi3usb9201), \
		    (USBC_PORT_BC12(usbc_id, bc12_id)), ())

#define BC12_CHIP(usbc_id)                                                   \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, bc12),                         \
		    (PI3SUSB9201_CHECK(usbc_id, DT_PHANDLE(usbc_id, bc12))), \
		    ())

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	DT_FOREACH_STATUS_OKAY(named_usbc_port, BC12_CHIP)
};

static void bc12_enable_irqs(void){
	DT_INST_FOREACH_STATUS_OKAY(BC12_GPIO_ENABLE_INTERRUPT)
} DECLARE_HOOK(HOOK_INIT, bc12_enable_irqs, HOOK_PRIO_DEFAULT);

#if DT_INST_NODE_HAS_PROP(0, irq)
void usb0_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(0, USB_CHG_EVENT_BC12);
}
#endif

#if DT_INST_NODE_HAS_PROP(1, irq)
void usb1_evt(enum gpio_signal signal)
{
	usb_charger_task_set_event(1, USB_CHG_EVENT_BC12);
}
#endif

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
