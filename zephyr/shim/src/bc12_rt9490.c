/* Copyright 2022 The ChromiumOS Authors
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

#define RT9490_DISPATCH_INTERRUPT(usbc_id, bc12_id)                        \
	IF_ENABLED(DT_NODE_HAS_PROP(bc12_id, irq),                         \
		   (case GPIO_SIGNAL(                                      \
			    DT_PHANDLE(DT_PHANDLE(bc12_id, irq), irq_pin)) \
		    : rt9490_interrupt(USBC_PORT_NEW(usbc_id));            \
		    break;))

#define RT9490_CHECK(usbc_id, bc12_id)                                \
	COND_CODE_1(DT_NODE_HAS_COMPAT(bc12_id, richtek_rt9490_bc12), \
		    (RT9490_DISPATCH_INTERRUPT(usbc_id, bc12_id)), ())

#define RT9490_INTERRUPT(usbc_id)                    \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, bc12), \
		    (RT9490_CHECK(usbc_id, DT_PHANDLE(usbc_id, bc12))), ())

void rt9490_bc12_dt_interrupt(enum gpio_signal signal)
{
	switch (signal) {
		DT_FOREACH_STATUS_OKAY(named_usbc_port, RT9490_INTERRUPT)
	default:
		break;
	}
}

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
