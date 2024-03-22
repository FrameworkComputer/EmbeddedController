/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Starmie PPC/BC12 (RT1739) configuration */

#include "baseboard_usbc_config.h"
#include "driver/ppc/rt1739.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "system.h"
#include "usb_mux.h"
#include "usbc/ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

LOG_MODULE_REGISTER(alt_dev_replacement);

static bool prob_alt_ppc(void)
{
	int rv;
	int val = 0;

	for (int i = 0; i < 3; i++) {
		rv = i2c_read8(ppc_chips[0].i2c_port,
			       ppc_chips[0].i2c_addr_flags, 0x00, &val);
		if (!rv) /* device acks */
			return false;

		rv = i2c_read8(ppc_chips[1].i2c_port,
			       ppc_chips[1].i2c_addr_flags, 0x00, &val);
		if (!rv) /* device acks */
			return true;
	}
	return false;
}

static void check_alternate_devices(void)
{
	/* Configure the PPC driver */
	if (prob_alt_ppc()) {
		CPRINTS("%s PPC_ENABLE_ALTERNATE(0)", __func__);
		/* Arg is the USB port number */
		PPC_ENABLE_ALTERNATE(0);
	}
}
DECLARE_HOOK(HOOK_INIT, check_alternate_devices, HOOK_PRIO_DEFAULT);

static void board_usbc_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_usb_c0_ppc_bc12));
}
DECLARE_HOOK(HOOK_INIT, board_usbc_init, HOOK_PRIO_POST_DEFAULT);

void ppc_interrupt(enum gpio_signal signal)
{
	if (signal == GPIO_SIGNAL(DT_NODELABEL(usb_c0_ppc_bc12_int_odl))) {
		ppc_chips[0].drv->interrupt(0);
	}
}

static int set_rt1739(void)
{
	/*
	 * (b:286803490#comment12)
	 * this is a workaround, we initialize rt1739 in an early stage to turn
	 * on an internal MOS, so the system can boot up with lower voltage. We
	 * only want to perform this workaround once, so we do it in RO, and not
	 * do it again in RW, otherwise,re-initialize rt1739 in RW again would
	 * cause a temporary voltage drop due to switching an internal MOS, and
	 * EC would have abnormal behaviors due to sensing the wrong voltage.
	 */
	if (!system_is_in_rw())
		rt1739_init(0);
	return 0;
}

SYS_INIT(set_rt1739, POST_KERNEL, 61);
