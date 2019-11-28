/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "config.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			 PDO_FIXED_EXTERNAL  | PDO_FIXED_COMM_CAP)

/* Threshold voltage of VBUS provided (mV) */
#define PD_VBUS_PROVIDED_THRESHOLD 3900

const uint32_t pd_src_pdo[] = {
	PDO_FIXED(5000, 1500, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

const uint32_t pd_snk_pdo[] = {
	PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
	PDO_BATT(4500, 14000, 10000),
	PDO_VAR(4500, 14000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

int pd_is_max_request_allowed(void)
{
	/* max voltage request allowed */
	return 1;
}

int pd_snk_is_vbus_provided(int port)
{
	int mv = adc_read_channel(port == USBPD_PORT_A ?
					ADC_VBUSSA : ADC_VBUSSB);

	/* level shift voltage of VBUS > threshold */
	return (mv * 23 / 3) > PD_VBUS_PROVIDED_THRESHOLD;
}

int pd_set_power_supply_ready(int port)
{
	/* provide VBUS */
	board_pd_vbus_ctrl(port, 1);
	/* vbus provided or not */
	return !pd_snk_is_vbus_provided(port);
}

void pd_power_supply_reset(int port)
{
	/* Kill VBUS */
	board_pd_vbus_ctrl(port, 0);
}


int pd_check_data_swap(int port, int data_role)
{
	/* Always allow data swap: we can be DFP or UFP for USB */
	return 1;
}

int pd_check_vconn_swap(int port)
{
	/*
	 * VCONN is provided directly by the battery(PPVAR_SYS)
	 * but use the same rules as power swap
	 */
	return pd_get_dual_role(port) == PD_DRP_TOGGLE_ON ? 1 : 0;
}
