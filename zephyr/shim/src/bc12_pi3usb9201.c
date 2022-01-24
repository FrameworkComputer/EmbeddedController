/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT pericom_pi3usb9201

#include <devicetree.h>
#include "bc12/pi3usb9201_public.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_pd.h"


#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) > 0,
		"No compatible BC1.2 instance found");

#define USBC_PORT_BC12(inst)                                                  \
	{                                                                     \
		.i2c_port = I2C_PORT(DT_PHANDLE(DT_DRV_INST(inst), port)),    \
		.i2c_addr_flags = DT_STRING_UPPER_TOKEN(                      \
					DT_DRV_INST(inst), i2c_addr_flags),   \
	},

const struct pi3usb9201_config_t pi3usb9201_bc12_chips[] = {
	DT_INST_FOREACH_STATUS_OKAY(USBC_PORT_BC12)
};

#define BC12_GPIO_ENABLE_INTERRUPT(inst)                          \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(inst, irq),		  \
		   (gpio_enable_dt_interrupt(			  \
			GPIO_INT_FROM_NODE(DT_INST_PHANDLE(inst, irq)))) \
		   );

static void bc12_enable_irqs(void)
{
	DT_INST_FOREACH_STATUS_OKAY(BC12_GPIO_ENABLE_INTERRUPT)
}
DECLARE_HOOK(HOOK_INIT, bc12_enable_irqs, HOOK_PRIO_DEFAULT);

#if DT_INST_NODE_HAS_PROP(0, irq)
void usb0_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P0, USB_CHG_EVENT_BC12);
}
#endif

#if DT_INST_NODE_HAS_PROP(1, irq)
void usb1_evt(enum gpio_signal signal)
{
	task_set_event(TASK_ID_USB_CHG_P1, USB_CHG_EVENT_BC12);
}
#endif

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
