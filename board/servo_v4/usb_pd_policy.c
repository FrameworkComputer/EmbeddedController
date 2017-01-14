/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Define typical operating power and max power */
/*#define OPERATING_POWER_MW 15000 */
/*#define MAX_POWER_MW       60000 */
/*#define MAX_CURRENT_MA     3000 */

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			 PDO_FIXED_COMM_CAP)

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   900, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_BATT(4750, 21000, 15000),
		PDO_VAR(4750, 21000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

int pd_is_valid_input_voltage(int mv)
{
	/* Any voltage less than the max is allowed */
	return 1;
}

void pd_transition_voltage(int idx)
{
	/*
	 * TODO(crosbug.com/p/60794): Most likely this function is a don't care
	 * for servo_v4 since VBUS provided to the DUT port has just an on/off
	 * control.  For now leave it as a no-op.
	 */
}

int pd_set_power_supply_ready(int port)
{
	/* Port 0 can never provide vbus. */
	if (!port)
		return EC_ERROR_INVAL;

	/*
	 * TODO(crosbug.com/p/60794): For now always assume VBUS is supplied by
	 * host. No support yet for using CHG VBUS passthru mode.
	 */

	/*
	 * Select Host as source for VBUS.
	 * To select host, set GPIO_HOST_OR_CHG_CTL low. To select CHG as VBUS
	 * source, then set GPIO_HOST_OR_CHG_CTL high.
	 */
	gpio_set_level(GPIO_HOST_OR_CHG_CTL, 0);

	/* Enable VBUS from the source selected above. */
	gpio_set_level(GPIO_DUT_CHG_EN, 1);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Disable VBUS */
	gpio_set_level(GPIO_DUT_CHG_EN, 0);
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	/*
	 * TODO(crosbug.com/p/60794): Placeholder for now so that can compile
	 * with USB PD support.
	 */
}

void typec_set_input_current_limit(int port, uint32_t max_ma,
				   uint32_t supply_voltage)
{
	/*
	 * TODO(crosbug.com/p/60794): Placeholder for now so that can compile
	 * with USB PD support.
	 */
}

int pd_snk_is_vbus_provided(int port)
{

	return gpio_get_level(port ? GPIO_USB_DET_PP_DUT :
				     GPIO_USB_DET_PP_CHG);
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/*
	 * TODO(crosbug.com/p/60792): CHG port can't do a power swap as it's SNK
	 * only. DUT port should be able to support a power role swap, but VBUS
	 * will need to be present. For now, don't allow swaps on either port.
	 */
	return 0;
}

int pd_check_data_swap(int port, int data_role)
{
	/* Servo can allow data role swaps */
	return 1;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Should we do something here? */
}

void pd_check_pr_role(int port, int pr_role, int flags)
{
	/*
	 * TODO(crosbug.com/p/60792): CHG port can't do a power swap as it's SNK
	 * only. DUT port should be able to support a power role swap, but VBUS
	 * will need to be present. For now, don't allow swaps on either port.
	 */

}

void pd_check_dr_role(int port, int dr_role, int flags)
{
	/*
	 * TODO(crosbug.com/p/60792): CHG port is SNK only and should not need
	 * to change from default UFP role. DUT port behavior needs to be
	 * flushed out. Don't request any data role change for either port for
	 * now.
	 */
}


/* ----------------- Vendor Defined Messages ------------------ */
const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);

	/* make sure we have some payload */
	if (cnt == 0)
		return 0;

	switch (cmd) {
	case VDO_CMD_VERSION:
		/* guarantee last byte of payload is null character */
		*(payload + cnt - 1) = 0;
		CPRINTF("ver: %s\n", (char *)(payload+1));
		break;
	case VDO_CMD_CURRENT:
		CPRINTF("Current: %dmA\n", payload[1]);
		break;
	}

	return 0;
}



const struct svdm_amode_fx supported_modes[] = {};
const int supported_modes_cnt = ARRAY_SIZE(supported_modes);
