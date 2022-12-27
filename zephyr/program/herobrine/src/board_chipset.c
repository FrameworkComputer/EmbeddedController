/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Herobrine chipset-specific configuration */

#include "battery.h"
#include "board_chipset.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "timer.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#define CPRINTS(format, args...) cprints(CC_HOOK, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_HOOK, format, ##args)

/*
 * A window of PD negotiation. It starts from the Type-C state reaching
 * Attached.SNK, and ends when the PD contract is created. The VBUS may be
 * raised anytime in this window.
 *
 * The current implementation is the worst case scenario: every message the PD
 * negotiation is received at the last moment before timeout. More extra time
 * is added to compensate the delay internally, like the decision of the DPM.
 *
 * TODO(waihong): Cancel this timer when the PD contract is negotiated.
 */
#define PD_READY_TIMEOUT                                                    \
	(PD_T_SINK_WAIT_CAP + PD_T_SENDER_RESPONSE + PD_T_SINK_TRANSITION + \
	 20 * MSEC)

#define PD_READY_POLL_DELAY (10 * MSEC)

static timestamp_t pd_ready_timeout;

static bool pp5000_inited;

__test_only void reset_pp5000_inited(void)
{
	pp5000_inited = false;
}

/* First boot, battery unattached, disconnected or low SOC */
static void check_delay_5v(void)
{
	int soc = -1;

	if (pp5000_inited)
		return;

	if (battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED) {
		CPRINTS("Delay 5V due to battery disconnect");
	} else if (battery_state_of_charge_abs(&soc) != EC_SUCCESS ||
		   soc < charger_get_min_bat_pct_for_power_on()) {
		CPRINTS("Delay 5V due to low battery");
	} else {
		return;
	}

	pd_ready_timeout = get_time();
	pd_ready_timeout.val += PD_READY_TIMEOUT;
}

/* Called on USB PD connected */
static void board_usb_pd_connect(void)
{
	check_delay_5v();
}
DECLARE_HOOK(HOOK_USB_PD_CONNECT, board_usb_pd_connect, HOOK_PRIO_DEFAULT);

static void wait_pd_ready(void)
{
	CPRINTS("Wait PD negotiated VBUS transition %u",
		pd_ready_timeout.le.lo);
	while (pd_ready_timeout.val && get_time().val < pd_ready_timeout.val)
		usleep(PD_READY_POLL_DELAY);
}

#define PPC_WAIT_5V_DELAY_MS 5

/* Called on AP S5 -> S3 transition */
static void board_chipset_pre_init(void)
{
	int port;

	if (pp5000_inited)
		return;

	check_delay_5v();

	if (pd_ready_timeout.val) {
		wait_pd_ready();
	}

	CPRINTS("Enable 5V rail");
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_pp5000_s5), 1);
	pp5000_inited = true;

	/*
	 * Due to the delay of the 5V rail enabling until 5V@3A is
	 * ready, the ppc_init may run when the PPC is not powered
	 * yet. So here rerunning the ppc_init function after the 5V
	 * rail enables to prevent Type-C port no function.
	 */
	msleep(PPC_WAIT_5V_DELAY_MS);
	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		if (pd_get_task_cc_state(port) != PD_CC_NONE)
			continue;
		ppc_init(port);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init, HOOK_PRIO_DEFAULT);
