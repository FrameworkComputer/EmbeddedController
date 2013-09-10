/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control for kirby board */

#include "battery.h"
#include "charger.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "task.h"
#include "timer.h"
#include "tsu6721.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_USBCHARGE, outstr)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

int extpower_is_present(void)
{
	return !gpio_get_level(GPIO_AC_PRESENT_L);
}

static void extpower_update_otg(void)
{
	int dev_type, is_otg;

	dev_type = tsu6721_get_device_type();
	is_otg = dev_type & TSU6721_TYPE_OTG;

	if (is_otg && !gpio_get_level(GPIO_BCHGR_OTG)) {
		charger_enable_otg_power(1);
		CPRINTF("[%T OTG power enabled]\n");
	} else if (!is_otg && gpio_get_level(GPIO_BCHGR_OTG)) {
		charger_enable_otg_power(0);
		CPRINTF("[%T OTG power disabled]\n");
	}
}

static void extpower_deferred(void)
{
	int int_val;
	int ac;
	static int last_ac = -1;

	int_val = tsu6721_get_interrupts();

	ac = extpower_is_present();
	if (last_ac != ac) {
		last_ac = ac;
		hook_notify(HOOK_AC_CHANGE);
	}

	if (!int_val)
		return;

	extpower_update_otg();
}
DECLARE_DEFERRED(extpower_deferred);

/*****************************************************************************/
/* Hooks */

static void extpower_init(void)
{
	tsu6721_reset();
	gpio_enable_interrupt(GPIO_USB_CHG_INT);
	gpio_enable_interrupt(GPIO_AC_PRESENT_L);
	extpower_update_otg();
}
DECLARE_HOOK(HOOK_INIT, extpower_init, HOOK_PRIO_LAST);

void extpower_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(extpower_deferred, 0);
}
