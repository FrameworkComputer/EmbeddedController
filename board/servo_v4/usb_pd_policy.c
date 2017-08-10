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
#include "tcpm.h"
#include "timer.h"
#include "util.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "usb_pd_tcpm.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define DUT_PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP |\
			     PDO_FIXED_COMM_CAP)

#define CHG_PDO_FIXED_FLAGS (PDO_FIXED_DATA_SWAP)

#define VBUS_UNCHANGED(curr, pend, new) (curr == new && pend == new)

/*
 * Dynamic PDO that reflects capabilities present on the CHG port. Allow for two
 * entries so that can offer greater than 5V charging. The 1st entry will be
 * fixed 5V, but its current value may change based on the CHG port vbus
 * info. The 2nd entry is used for when offering vbus greater than 5V.
 */
static uint32_t pd_src_chg_pdo[2];
static uint8_t chg_pdo_cnt;
static const uint32_t pd_src_host_pdo[] = {
		PDO_FIXED(5000, 500, DUT_PDO_FIXED_FLAGS),
};
const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, CHG_PDO_FIXED_FLAGS),
		PDO_BATT(4750, 21000, 15000),
		PDO_VAR(4750, 21000, 3000),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

struct vbus_prop {
	int mv;
	int ma;
};
static struct vbus_prop vbus[CONFIG_USB_PD_PORT_COUNT];
static struct vbus_prop vbus_pend;
static uint8_t vbus_rp = TYPEC_RP_RESERVED;
static int disable_dts_mode;

/* Voltage thresholds for no connect in DTS mode */
static int pd_src_vnc_dts[TYPEC_RP_RESERVED][2] = {
	{PD_SRC_3_0_VNC_MV, PD_SRC_1_5_VNC_MV},
	{PD_SRC_1_5_VNC_MV, PD_SRC_DEF_VNC_MV},
	{PD_SRC_3_0_VNC_MV, PD_SRC_DEF_VNC_MV},
};
/* Voltage thresholds for Ra attach in DTS mode */
static int pd_src_rd_threshold_dts[TYPEC_RP_RESERVED][2] = {
	{PD_SRC_3_0_RD_THRESH_MV, PD_SRC_1_5_RD_THRESH_MV},
	{PD_SRC_1_5_RD_THRESH_MV, PD_SRC_DEF_RD_THRESH_MV},
	{PD_SRC_3_0_RD_THRESH_MV, PD_SRC_DEF_RD_THRESH_MV},
};
/* Voltage thresholds for no connect in normal SRC mode */
static int pd_src_vnc[TYPEC_RP_RESERVED] = {
	PD_SRC_DEF_VNC_MV,
	PD_SRC_1_5_VNC_MV,
	PD_SRC_3_0_VNC_MV,
};
/* Voltage thresholds for Ra attach in normal SRC mode */
static int pd_src_rd_threshold[TYPEC_RP_RESERVED] = {
	PD_SRC_DEF_RD_THRESH_MV,
	PD_SRC_1_5_RD_THRESH_MV,
	PD_SRC_3_0_RD_THRESH_MV,
};

static void board_manage_dut_port(void)
{
	int rp;

	/*
	 * This function is called by the CHG port whenever there has been a
	 * change in its vbus voltage or current. That change may necessitate
	 * that the DUT port present a different Rp value or renogiate its PD
	 * contract if it is connected.
	 */

	/* Assume the default value of Rp */
	rp = TYPEC_RP_USB;
	if (vbus[CHG].mv == 5000) {
		/* Only advertise higher current via Rp if vbus == 5V */
		if (vbus[CHG].ma >= 3000)
			/* CHG port is connected and DUt can advertise 3A */
			rp = TYPEC_RP_3A0;
		else if (vbus[CHG].ma >= 1500)
			rp = TYPEC_RP_1A5;
	}

	/* Check if Rp setting needs to change from current value */
	if (vbus_rp != rp)
		/* Present new Rp value */
		tcpm_select_rp_value(DUT, rp);

	/*
	 * If CHG vbus voltage/current doesn't match the DUT port value, then
	 * update the contract. If DUT port is not in the correct state, then
	 * this call does nothing.
	 */
	if (vbus[CHG].mv != vbus[DUT].mv || vbus[CHG].ma != vbus[DUT].ma)
		/*
		 * Update PD contract to reflect new available CHG
		 * voltage/current values.
		 */
		pd_update_contract(DUT);
}

static void board_manage_chg_port(void)
{
	/* Update the voltage/current values for CHG port */
	vbus[CHG].mv = vbus_pend.mv;
	vbus[CHG].ma = vbus_pend.ma;

	/*
	 * CHG Vbus has changed states, update PDO that reflects CHG port
	 * state
	 */
	if (!vbus[CHG].mv) {
		/* CHG Vbus has dropped, so always source DUT Vbus from host */
		gpio_set_level(GPIO_HOST_OR_CHG_CTL, 0);
		chg_pdo_cnt = 0;
	} else {
		int v5_ma = vbus[CHG].mv > 5000 ? 500 : vbus[CHG].ma;

		pd_src_chg_pdo[0] = PDO_FIXED_VOLT(5000) |
			PDO_FIXED_CURR(v5_ma) | DUT_PDO_FIXED_FLAGS |
			PDO_FIXED_EXTERNAL;

		chg_pdo_cnt = 1;
		if (vbus[CHG].mv > 5000) {
			/*
			 * CHG vbus is > 5V so need an entry for vSafe5V and an
			 * entry that reflects CHG VBUS
			 */
			pd_src_chg_pdo[1] = PDO_FIXED_VOLT(vbus[CHG].mv) |
				PDO_FIXED_CURR(vbus[CHG].ma) |
				DUT_PDO_FIXED_FLAGS | PDO_FIXED_EXTERNAL;
			chg_pdo_cnt = 2;
		}
	}

	/* Call DUT port manager to update Rp and possible PD contract */
	board_manage_dut_port();
}
DECLARE_DEFERRED(board_manage_chg_port);

static void board_update_chg_vbus(int max_ma, int vbus_mv)
{
	/*
	 * Determine if vbus from CHG port has changed values and if the current
	 * state of CHG vbus is on or off. If the change is on, then schedule a
	 * deferred callback. If the change is off, then act immediately.
	 */
	if (VBUS_UNCHANGED(vbus[CHG].mv, vbus_pend.mv, vbus_mv) &&
	    VBUS_UNCHANGED(vbus[CHG].ma, vbus_pend.ma, max_ma))
		/* No change in CHG VBUS detected, nothing else to do. */
		return;

	/* CHG port voltage and current values are now pending */
	vbus_pend.mv = vbus_mv;
	vbus_pend.ma = max_ma;

	if (vbus_mv)
		/* Wait enough time for PD contract to be established */
		hook_call_deferred(&board_manage_chg_port_data,
				  PD_T_SINK_WAIT_CAP * 3);
	else
		/* Update CHG port status now since vbus is off */
		hook_call_deferred(&board_manage_chg_port_data, 0);
}

int pd_tcpc_cc_nc(int port, int cc_volt, int cc_sel)
{
	int rp_index;
	int nc;

	/* Can never be called from CHG port as it's sink only */
	if (port == CHG)
		return 0;

	rp_index = vbus_rp;
	/*
	 * If rp_index > 2, then always return not connected. This case should
	 * only happen when all Rp GPIO controls are tri-stated.
	 */
	if (rp_index >= TYPEC_RP_RESERVED)
		return 1;

	/* Select the correct voltage threshold for current Rp and DTS mode */
	if (disable_dts_mode)
		nc = cc_volt >= pd_src_vnc[rp_index];
	else
		nc = cc_volt >= pd_src_vnc_dts[rp_index][cc_sel];

	return nc;
}

int pd_tcpc_cc_ra(int port, int cc_volt, int cc_sel)
{
	int rp_index;
	int ra;

	/* Can never be called from CHG port as it's sink only */
	if (port == CHG)
		return 0;

	rp_index = vbus_rp;
	/*
	 * If rp_index > 2, then can't be Ra. This case should
	 * only happen when all Rp GPIO controls are tri-stated.
	 */
	if (rp_index >= TYPEC_RP_RESERVED)
		return 0;

	/* Select the correct voltage threshold for current Rp and DTS mode */
	if (disable_dts_mode)
		ra = cc_volt < pd_src_rd_threshold[rp_index];
	else
		ra = cc_volt < pd_src_rd_threshold_dts[rp_index][cc_sel];

	return ra;
}

static int board_set_rp(int rp)
{
	if (disable_dts_mode) {
		/*
		 * DTS mode is disabled, so only present the requested Rp value
		 * on CC1 and leave all Rp/Rd resistors on CC2 disconnected.
		 */
		switch (rp) {
		case TYPEC_RP_USB:
			gpio_set_flags(GPIO_USB_DUT_CC1_RPUSB, GPIO_OUT_HIGH);
			break;
		case TYPEC_RP_1A5:
			gpio_set_flags(GPIO_USB_DUT_CC1_RP1A5, GPIO_OUT_HIGH);
			break;
		case TYPEC_RP_3A0:
			gpio_set_flags(GPIO_USB_DUT_CC1_RP3A0, GPIO_OUT_HIGH);
			break;
		case TYPEC_RP_RESERVED:
			/*
			 * This case can be used to force a detach event since
			 * all values are set to inputs above. Nothing else to
			 * set.
			 */
			break;
		default:
			return EC_ERROR_INVAL;
		}
	} else {
		/* DTS mode is enabled. The rp parameter is used to select the
		 * Type C current limit to advertise. The combinations of Rp on
		 * each CC line is shown in the table below.
		 *
		 * CC values for Debug sources (DTS)
		 *
		 * Source type  Mode of Operation   CC1    CC2
		 * ---------------------------------------------
		 * DTS          Default USB Power   Rp3A0  Rp1A5
		 * DTS          USB-C @ 1.5 A       Rp1A5  RpUSB
		 * DTS          USB-C @ 3 A         Rp3A0  RpUSB
		 */
		switch (rp) {
		case TYPEC_RP_USB:
			gpio_set_flags(GPIO_USB_DUT_CC1_RP3A0, GPIO_OUT_HIGH);
			gpio_set_flags(GPIO_USB_DUT_CC2_RP1A5, GPIO_OUT_HIGH);
			break;
		case TYPEC_RP_1A5:
			gpio_set_flags(GPIO_USB_DUT_CC1_RP1A5, GPIO_OUT_HIGH);
			gpio_set_flags(GPIO_USB_DUT_CC2_RPUSB, GPIO_OUT_HIGH);
			break;
		case TYPEC_RP_3A0:
			gpio_set_flags(GPIO_USB_DUT_CC1_RP3A0, GPIO_OUT_HIGH);
			gpio_set_flags(GPIO_USB_DUT_CC2_RPUSB, GPIO_OUT_HIGH);
			break;
		case TYPEC_RP_RESERVED:
			/*
			 * This case can be used to force a detach event since
			 * all values are set to inputs above. Nothing else to
			 * set.
			 */
			break;
		default:
			return EC_ERROR_INVAL;
		}
	}
	/* Save new Rp value for DUT port */
	vbus_rp = rp;

	return EC_SUCCESS;
}

int pd_set_rp_rd(int port, int cc_pull, int rp_value)
{
	int rv = EC_SUCCESS;

	/* By default disconnect all Rp/Rd resistors from both CC lines */
	/* Set Rd for CC1/CC2 to High-Z. */
	gpio_set_flags(GPIO_USB_DUT_CC1_RD, GPIO_INPUT);
	gpio_set_flags(GPIO_USB_DUT_CC2_RD, GPIO_INPUT);
	/* Set Rp for CC1/CC2 to High-Z. */
	gpio_set_flags(GPIO_USB_DUT_CC1_RP3A0, GPIO_INPUT);
	gpio_set_flags(GPIO_USB_DUT_CC2_RP3A0, GPIO_INPUT);
	gpio_set_flags(GPIO_USB_DUT_CC1_RP1A5, GPIO_INPUT);
	gpio_set_flags(GPIO_USB_DUT_CC2_RP1A5, GPIO_INPUT);
	gpio_set_flags(GPIO_USB_DUT_CC1_RPUSB, GPIO_INPUT);
	gpio_set_flags(GPIO_USB_DUT_CC2_RPUSB, GPIO_INPUT);

	/* Set TX Hi-Z */
	gpio_set_flags(GPIO_USB_DUT_CC1_TX_DATA, GPIO_INPUT);
	gpio_set_flags(GPIO_USB_DUT_CC2_TX_DATA, GPIO_INPUT);

	if (cc_pull == TYPEC_CC_RP) {
		rv = board_set_rp(rp_value);
	} else if (cc_pull == TYPEC_CC_RD) {
		/*
		 * The DUT port uses a captive cable. If can present Rd on both
		 * CC1 and CC2. If DTS mode is enabled, then present Rd on both
		 * CC lines. However, if DTS mode is disabled only present Rd on
		 * CC1.
		 */
		gpio_set_flags(GPIO_USB_DUT_CC1_RD, GPIO_OUT_LOW);
		if (!disable_dts_mode)
			gpio_set_flags(GPIO_USB_DUT_CC2_RD, GPIO_OUT_LOW);
	}

	return rv;
}

int board_select_rp_value(int port, int rp)
{
	return pd_set_rp_rd(port, TYPEC_CC_RP, rp);
}

int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	int pdo_cnt;
	/*
	 * If CHG is providing VBUS, then advertise what's available on the CHG
	 * port, otherwise used the fixed value that matches host capabilities.
	 */
	if (vbus[CHG].mv) {
		*src_pdo =  pd_src_chg_pdo;
		pdo_cnt = chg_pdo_cnt;
	} else {
		*src_pdo =  pd_src_host_pdo;
		pdo_cnt = ARRAY_SIZE(pd_src_host_pdo);
	}

	return pdo_cnt;
}

int pd_is_valid_input_voltage(int mv)
{
	/* Any voltage less than the max is allowed */
	return 1;
}

void pd_transition_voltage(int idx)
{
	/*
	 * Up to this point, VBUS will have been supplied by host. If
	 * vbus[CHG].mv is set, then that means the CHG port is in a steady
	 * state condition and its voltage/current values have been communicated
	 * to the DUT in the SRC_CAP message. If CHG vbus > 5V then 2
	 * chg_src_pdo entries will have been sent. Only allow pass through
	 * charging from CHG vbus if the pdo idx requested by the DUT matches
	 * the number of chg_src_pdo entries.
	 *
	 */
	if (vbus[CHG].mv && idx == chg_pdo_cnt) {
		gpio_set_level(GPIO_HOST_OR_CHG_CTL, 1);
		/*
		 * VBUS being provided by CHG port now. Update DUT port's vbus
		 * info with the CHG port's values.
		 */
		vbus[DUT].mv = vbus[CHG].mv;
		vbus[DUT].ma = vbus[CHG].ma;
	}
}

int pd_set_power_supply_ready(int port)
{
	/* Port 0 can never provide vbus. */
	if (port == CHG)
		return EC_ERROR_INVAL;

	/* Only ever allow host vbus at this point */
	gpio_set_level(GPIO_HOST_OR_CHG_CTL, 0);

	/* Enable VBUS */
	gpio_set_level(GPIO_DUT_CHG_EN, 1);

	/* Host vbus is always 5V/500mA */
	vbus[DUT].mv = 5000;
	vbus[DUT].ma = 500;

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Port 0 can never provide vbus. */
	if (port == CHG)
		return;

	/* Disable VBUS */
	gpio_set_level(GPIO_DUT_CHG_EN, 0);
	/* Set default VBUS source to Host */
	gpio_set_level(GPIO_HOST_OR_CHG_CTL, 0);

	/* Host vbus is always 5V/500mA */
	vbus[DUT].mv = 0;
	vbus[DUT].ma = 0;
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	if (port == CHG)
		board_update_chg_vbus(max_ma, supply_voltage);
}

void typec_set_input_current_limit(int port, uint32_t max_ma,
				   uint32_t supply_voltage)
{
	if (port == CHG)
		board_update_chg_vbus(max_ma, supply_voltage);
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
	 * When only host VBUS is available, then servo_v4 is not setting
	 * PDO_FIXED_EXTERNAL in the src_pdo sent to the DUT. When this bit is
	 * not set, the DUT will always attempt to swap its power role to
	 * SRC. Let servo_v4 have more control over its power role by always
	 * rejecting power swap requests from the DUT.
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
	if (port == CHG)
		return;

	/* If DFP, try to switch to UFP */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_DFP)
		pd_request_data_swap(port);
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

static int command_dts(int argc, char **argv)
{
	int disable_dts_new;
	int val;

	if (argc < 2) {
		/* Get current CCD status */
		ccprintf("dts mode: %s\n", disable_dts_mode ? "off" : "on");
		return EC_SUCCESS;
	}

	if (!parse_bool(argv[1], &val))
		return EC_ERROR_PARAM2;

	disable_dts_new = val ^ 1;
	if (disable_dts_new != disable_dts_mode) {
		/* Force detach */
		pd_power_supply_reset(DUT);
		/* Always set to 0 here so both CC lines are changed */
		disable_dts_mode = 0;
		/* Remove Rp/Rd on both CC lines */
		board_select_rp_value(DUT, TYPEC_RP_RESERVED);
		/* Accept new disable_dts value */
		disable_dts_mode = disable_dts_new;
		/* Some time for DUT to detach */
		msleep(100);
		/* Present RP_USB on CC1 and CC2 based on disable_dts_mode */
		board_select_rp_value(DUT, TYPEC_RP_USB);
		ccprintf("dts mode: %s\n", disable_dts_mode ? "off" : "on");
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dts, command_dts,
			"off|on",
			"Servo_v4 DTS mode on/off");
