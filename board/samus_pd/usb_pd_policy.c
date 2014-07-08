/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* TODO(crossbug.com/p/28869): update source and sink tables to spec. */
const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   500, PDO_FIXED_EXTERNAL),
		PDO_FIXED(5000,   900, 0),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

/* TODO(crossbug.com/p/28869): update source and sink tables to spec. */
const uint32_t pd_snk_pdo[] = {
		PDO_BATT(4500,   5500, 15000),
		PDO_BATT(11500, 12500, 36000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Cap on the max voltage requested as a sink (in millivolts) */
static unsigned max_mv = -1; /* no cap */

/* Flag for battery status */
static int battery_ok = 1;

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo)
{
	int i;
	int sel_mv;
	int max_uw = 0;
	int max_i = -1;

	/* Don't negotiate power until battery ok signal is given */
	if (!battery_ok)
		return -EC_ERROR_UNKNOWN;

	/* Get max power */
	for (i = 0; i < cnt; i++) {
		int uw;
		int mv = ((src_caps[i] >> 10) & 0x3FF) * 50;
		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
			uw = 250000 * (src_caps[i] & 0x3FF);
		} else {
			int ma = (src_caps[i] & 0x3FF) * 10;
			uw = ma * mv;
		}
		if ((uw > max_uw) && (mv <= max_mv)) {
			max_i = i;
			max_uw = uw;
			sel_mv = mv;
		}
	}
	if (max_i < 0)
		return -EC_ERROR_UNKNOWN;

	/* request all the power ... */
	if ((src_caps[max_i] & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		int uw = 250000 * (src_caps[i] & 0x3FF);
		*rdo = RDO_BATT(max_i + 1, uw/2, uw, 0);
		ccprintf("Request [%d] %dV %d/%d mW\n",
			 max_i, sel_mv/1000, uw/1000, uw/1000);
	} else {
		int ma = 10 * (src_caps[max_i] & 0x3FF);
		*rdo = RDO_FIXED(max_i + 1, ma / 2, ma, 0);
		ccprintf("Request [%d] %dV %d/%d mA\n",
			 max_i, sel_mv/1000, max_i, ma/2, ma);
	}
	return EC_SUCCESS;
}

void pd_set_max_voltage(unsigned mv)
{
	max_mv = mv;
}

int pd_request_voltage(uint32_t rdo)
{
	int op_ma = rdo & 0x3FF;
	int max_ma = (rdo >> 10) & 0x3FF;
	int idx = rdo >> 28;
	uint32_t pdo;
	uint32_t pdo_ma;

	if (!idx || idx > pd_src_pdo_cnt)
		return EC_ERROR_INVAL; /* Invalid index */

	/* check current ... */
	pdo = pd_src_pdo[idx - 1];
	pdo_ma = (pdo & 0x3ff);
	if (op_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much op current */
	if (max_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much max current */

	ccprintf("Switch to %d V %d mA (for %d/%d mA)\n",
		 ((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		 ((rdo >> 10) & 0x3ff) * 10, (rdo & 0x3ff) * 10);

	return EC_SUCCESS;
}

int pd_set_power_supply_ready(void)
{
	/* provide VBUS */
	gpio_set_level(GPIO_USB_C0_5V_EN, 1);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(void)
{
	/* Kill VBUS */
	gpio_set_level(GPIO_USB_C0_5V_EN, 0);
}

static void pd_send_ec_int(void)
{
	gpio_set_level(GPIO_EC_INT_L, 0);

	/*
	 * Delay long enough to guarantee EC see's the change. Slowest
	 * EC clock speed is 250kHz in deep sleep -> 4us, and add 1us
	 * for buffer.
	 */
	usleep(5);

	gpio_set_level(GPIO_EC_INT_L, 1);
}

int pd_board_checks(void)
{
	static uint64_t last_time;

	/*
	 * If battery is not yet ok, signal EC to send status. Avoid
	 * sending requests too frequently.
	 */
	if (!battery_ok && (get_time().val - last_time >= SECOND)) {
		last_time = get_time().val;
		pd_send_ec_int();
	}

	return EC_SUCCESS;
}

int pd_power_negotiation_allowed(void)
{
	return battery_ok;
}

static void dual_role_on(void)
{
	pd_set_dual_role(PD_DRP_TOGGLE_ON);
	CPRINTS("PCH -> S0, enable dual-role toggling");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, dual_role_on, HOOK_PRIO_DEFAULT);

static void dual_role_off(void)
{
	pd_set_dual_role(PD_DRP_TOGGLE_OFF);
	CPRINTS("PCH -> S3, disable dual-role toggling");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, dual_role_off, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, dual_role_off, HOOK_PRIO_DEFAULT);

static void dual_role_force_sink(void)
{
	pd_set_dual_role(PD_DRP_FORCE_SINK);
	CPRINTS("PCH -> S5, force dual-role port to sink");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, dual_role_force_sink, HOOK_PRIO_DEFAULT);

static int command_ec_int(int argc, char **argv)
{
	pd_send_ec_int();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ecint, command_ec_int,
			"",
			"Toggle EC interrupt line",
			NULL);

static int ec_status_host_cmd(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_status *p = args->params;
	struct ec_response_pd_status *r = args->response;

	if (p->batt_soc >= CONFIG_USB_PD_MIN_BATT_CHARGE) {
		/*
		 * When battery is above minimum charge, we know
		 * that we have enough power remaining for us to
		 * negotiate power over PD.
		 */
		CPRINTS("Battery is ok, safe to negotiate power");
		battery_ok = 1;
	} else {
		battery_ok = 0;
	}

	r->status = 0;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_EXCHANGE_STATUS, ec_status_host_cmd,
			EC_VER_MASK(0));
