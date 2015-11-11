/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_EXTERNAL | PDO_FIXED_DATA_SWAP)

const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,  3000, PDO_FIXED_FLAGS),
		/* TODO: Add additional source modes when tested */
		/* PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS), */
		/* PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS), */
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

static const int pd_src_pdo_cnts[3] = {
		[SRC_CAP_5V] = 1,
		/* [SRC_CAP_12V] = 2, */
		/* [SRC_CAP_20V] = 3, */
};

static int pd_src_pdo_idx;

int pd_get_source_pdo(const uint32_t **src_pdo)
{
	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnts[pd_src_pdo_idx];
}

int pd_is_valid_input_voltage(int mv)
{
	return 1;
}

void pd_transition_voltage(int idx)
{
	/* No-operation: we are always 5V */
}

int pd_set_power_supply_ready(int port)
{
	CPRINTS("Power supply ready/%d", port);
	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	CPRINTS("Power supply reset/%d", port);
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

void pd_check_dr_role(int port, int dr_role, int flags)
{
	/* If DFP, try to switch to UFP */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_DFP)
		pd_request_data_swap(port);
}

void pd_check_pr_role(int port, int pr_role, int flags)
{
}

int pd_check_data_swap(int port, int data_role)
{
	/* Allow data swap if we are a DFP, otherwise don't allow */
	return (data_role == PD_ROLE_DFP) ? 1 : 0;
}

void pd_execute_data_swap(int port, int data_role)
{
	/* Do nothing */
}

/* ----------------- Vendor Defined Messages ------------------ */
/* TODO: Add identify and GFU modes similar to Zinger */
const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		  uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	uint16_t dev_id = 0;
	int is_rw;

	/* make sure we have some payload */
	if (cnt == 0)
		return 0;

	switch (cmd) {
	case VDO_CMD_VERSION:
		/* guarantee last byte of payload is null character */
		*(payload + cnt - 1) = 0;
		CPRINTF("version: %s\n", (char *)(payload+1));
		break;
	case VDO_CMD_READ_INFO:
	case VDO_CMD_SEND_INFO:
		/* copy hash */
		if (cnt == 7) {
			dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
			is_rw = VDO_INFO_IS_RW(payload[6]);

			CPRINTF("DevId:%d.%d SW:%d RW:%d\n",
				HW_DEV_ID_MAJ(dev_id),
				HW_DEV_ID_MIN(dev_id),
				VDO_INFO_SW_DBG_VER(payload[6]),
				is_rw);
		} else if (cnt == 6) {
			/* really old devices don't have last byte */
			pd_dev_store_rw_hash(port, dev_id, payload + 1,
					     SYSTEM_IMAGE_UNKNOWN);
		}
		break;
	}

	return 0;
}
