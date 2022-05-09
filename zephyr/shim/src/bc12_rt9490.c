/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT richtek_rt9490_bc12

#include <zephyr/devicetree.h>
#include "driver/charger/rt9490.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "usbc/utils.h"

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static void rt9490_bc12_enable_irqs(void)
{
	DT_INST_FOREACH_STATUS_OKAY(BC12_GPIO_ENABLE_INTERRUPT);
}
DECLARE_HOOK(HOOK_INIT, rt9490_bc12_enable_irqs, HOOK_PRIO_DEFAULT);

#define GPIO_SIGNAL_FROM_INST(inst)					\
	GPIO_SIGNAL(DT_PHANDLE(DT_INST_PHANDLE(inst, irq), irq_pin))

#define RT9490_DISPATCH_INTERRUPT(inst)					\
	IF_ENABLED(DT_INST_NODE_HAS_PROP(inst, irq),			\
		   (case GPIO_SIGNAL_FROM_INST(inst):			\
			rt9490_interrupt(USBC_PORT_FROM_INST(inst));	\
			break;						\
		   ))

void rt9490_bc12_dt_interrupt(enum gpio_signal signal)
{
	switch (signal) {
		DT_INST_FOREACH_STATUS_OKAY(RT9490_DISPATCH_INTERRUPT);
	default:
		break;
	}
}

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
