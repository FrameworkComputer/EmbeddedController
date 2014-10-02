/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
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

/* PD MCU status for host response */
static struct ec_response_pd_status pd_status;

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo)
{
	int i;
	int sel_mv;
	int max_uw = 0;
	int max_ma;
	int max_i = -1;

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
		int uw = 250000 * (src_caps[max_i] & 0x3FF);
		max_ma = uw / sel_mv;
		*rdo = RDO_BATT(max_i + 1, uw/2, uw, 0);
		ccprintf("Request [%d] %dV %dmW\n",
			 max_i, sel_mv/1000, uw/1000);
	} else {
		int ma = 10 * (src_caps[max_i] & 0x3FF);
		max_ma = ma;
		*rdo = RDO_FIXED(max_i + 1, ma / 2, ma, 0);
		ccprintf("Request [%d] %dV %dmA\n",
			 max_i, sel_mv/1000, ma);
	}
	return max_ma;
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

int pd_set_power_supply_ready(int port)
{
	/* provide VBUS */
	gpio_set_level(port ? GPIO_USB_C1_5V_EN : GPIO_USB_C0_5V_EN, 1);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	gpio_set_level(port ? GPIO_USB_C1_5V_EN : GPIO_USB_C0_5V_EN, 0);
}

static void pd_send_ec_int(void)
{
	gpio_set_level(GPIO_EC_INT, 1);

	/*
	 * Delay long enough to guarantee EC see's the change. Slowest
	 * EC clock speed is 250kHz in deep sleep -> 4us, and add 1us
	 * for buffer.
	 */
	usleep(5);

	gpio_set_level(GPIO_EC_INT, 0);
}

void pd_set_input_current_limit(uint32_t max_ma)
{
	pd_status.curr_lim_ma = max_ma;
	pd_send_ec_int();
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

/* ----------------- Vendor Defined Messages ------------------ */
int pd_custom_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	uint8_t dev_id = 0;
	ccprintf("VDM/%d [%d] %08x\n", cnt, cmd, payload[0]);

	/* make sure we have some payload */
	if (cnt == 0)
		return 0;

	switch (cmd) {
	case VDO_CMD_VERSION:
		/* guarantee last byte of payload is null character */
		*(payload + cnt - 1) = 0;
		ccprintf("version: %s\n", (char *)(payload+1));
		break;
	case VDO_CMD_READ_INFO:
	case VDO_CMD_SEND_INFO:
		/* if last word is present, it contains lots of info */
		if (cnt == 7) {
			dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
			ccprintf("Dev:%d SW:%d RW:%d\n", dev_id,
				 VDO_INFO_SW_DBG_VER(payload[6]),
				 VDO_INFO_IS_RW(payload[6]));
		}
		/* copy hash */
		if (cnt >= 6)
			pd_dev_store_rw_hash(port, dev_id, payload + 1);

		break;
	case VDO_CMD_CURRENT:
		ccprintf("Current: %dmA\n", payload[1]);
		break;
	}

	return 0;
}

/****************************************************************************/
/* Console commands */
static int command_ec_int(int argc, char **argv)
{
	pd_send_ec_int();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ecint, command_ec_int,
			"",
			"Toggle EC interrupt line",
			NULL);

static int command_pd_host_event(int argc, char **argv)
{
	atomic_or(&(pd_status.status), PD_STATUS_HOST_EVENT);
	pd_send_ec_int();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pdevent, command_pd_host_event,
			"",
			"Send PD host event",
			NULL);

/****************************************************************************/
/* Host commands */
static int ec_status_host_cmd(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_status *p = args->params;
	struct ec_response_pd_status *r = args->response;

	board_update_battery_soc(p->batt_soc);

	*r = pd_status;

	/* Clear host event */
	atomic_clear(&(pd_status.status), PD_STATUS_HOST_EVENT);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_EXCHANGE_STATUS, ec_status_host_cmd,
			EC_VER_MASK(0));
