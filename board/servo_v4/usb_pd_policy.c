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
 * Dynamic PDO that reflects capabilities present on the CHG port. Allow for
 * multiple entries so that we can offer greater than 5V charging. The 1st
 * entry will be fixed 5V, but its current value may change based on the CHG
 * port vbus info. Subsequent entries are used for when offering vbus greater
 * than 5V.
 */
static const uint16_t pd_src_voltages_mv[] = {
		5000, 9000, 12000, 15000, 20000,
};
static uint32_t pd_src_chg_pdo[ARRAY_SIZE(pd_src_voltages_mv)];
static uint8_t chg_pdo_cnt;

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
static int active_charge_port = CHARGE_PORT_NONE;
static enum charge_supplier active_charge_supplier;
static uint8_t vbus_rp = TYPEC_RP_RESERVED;

/* Flag to emulate detach, i.e. making both CC lines open. */
static int disable_cc;
/*
 * DTS mode: enabled connects resistors to both CC line to activate cr50,
 * disabled connects to one only as in the standard USBC cable.
 */
static int disable_dts_mode;
/* Do we allow charge through by policy? */
static int allow_src_mode = 1;

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

/* Saved value for the duration of faking PD disconnect */
static int fake_pd_disconnect_duration_us;

/*
 * Set the USB PD max voltage to value appropriate for the board version.
 * The red/blue versions of servo_v4 have an ESD between VBUS and CC1/CC2
 * that has a breakdown voltage of 11V.
 */
#define MAX_MV_RED_BLUE 9000

static uint32_t max_supported_voltage(void)
{
	return board_get_version() >= BOARD_VERSION_BLACK ?
		PD_MAX_VOLTAGE_MV : MAX_MV_RED_BLUE;
}

static int charge_port_is_active(void)
{
	return active_charge_port == CHG && vbus[CHG].mv > 0;
}

static void dut_allow_charge(void)
{
	/*
	 * Update to charge enable if charger still present and not
	 * already charging.
	 */
	if (charge_port_is_active() && allow_src_mode &&
	    pd_get_dual_role(DUT) != PD_DRP_FORCE_SOURCE) {
		CPRINTS("Enable DUT charge through");
		pd_set_dual_role(DUT, PD_DRP_FORCE_SOURCE);
		pd_config_init(DUT, PD_ROLE_SOURCE);
		pd_update_contract(DUT);
	}
}
DECLARE_DEFERRED(dut_allow_charge);

static void board_manage_dut_port(void)
{
	enum pd_dual_role_states allowed_role;
	enum pd_dual_role_states current_role;

	/*
	 * This function is called by the CHG port whenever there has been a
	 * change in its vbus voltage or current. That change may necessitate
	 * that the DUT port present a different Rp value or renogiate its PD
	 * contract if it is connected.
	 */

	/* Assume the default value of Rd */
	allowed_role = PD_DRP_FORCE_SINK;

	/* If VBUS charge through is available, mark as such. */
	if (charge_port_is_active() && allow_src_mode)
		allowed_role = PD_DRP_FORCE_SOURCE;

	current_role = pd_get_dual_role(DUT);
	if (current_role != allowed_role) {
		/* Update role. */
		if (allowed_role == PD_DRP_FORCE_SINK) {
			/* We've lost charge through. Disable VBUS. */
			gpio_set_level(GPIO_DUT_CHG_EN, 0);

			/* Mark as SNK only. */
			pd_set_dual_role(DUT, PD_DRP_FORCE_SINK);
			pd_config_init(DUT, PD_ROLE_SINK);
		} else {
			/* Allow charge through after PD negotiate. */
			hook_call_deferred(&dut_allow_charge_data, 2000 * MSEC);
		}
	}

	/*
	 * Update PD contract to reflect new available CHG
	 * voltage/current values.
	 */
	pd_update_contract(DUT);
}

static void update_ports(void)
{
	int pdo_index, src_index, snk_index, i;
	uint32_t pdo, max_ma, max_mv;

	/*
	 * CHG Vbus has changed states, update PDO that reflects CHG port
	 * state
	 */
	if (!charge_port_is_active()) {
		/* CHG Vbus has dropped, so become SNK. */
		chg_pdo_cnt = 0;
	} else {
		/* Advertise the 'best' PDOs at various discrete voltages */
		if (active_charge_supplier == CHARGE_SUPPLIER_PD) {
			src_index = 0;
			snk_index = -1;

			for (i = 0; i < ARRAY_SIZE(pd_src_voltages_mv); ++i) {
				/* Adhere to board voltage limits */
				if (pd_src_voltages_mv[i] >
				    max_supported_voltage())
					break;
				/* Find the 'best' PDO <= voltage */
				pdo_index = pd_find_pdo_index(
					CHG, pd_src_voltages_mv[i], &pdo);
				/* Don't duplicate PDOs */
				if (pdo_index == snk_index)
					continue;
				/* Skip battery / variable PDOs */
				if ((pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
					continue;

				snk_index = pdo_index;
				pd_extract_pdo_power(pdo, &max_ma, &max_mv);
				pd_src_chg_pdo[src_index++] =
					PDO_FIXED_VOLT(max_mv) |
					PDO_FIXED_CURR(max_ma) |
					DUT_PDO_FIXED_FLAGS |
					PDO_FIXED_EXTERNAL;
			}
			chg_pdo_cnt = src_index;
		} else {
			/* 5V PDO */
			pd_src_chg_pdo[0] = PDO_FIXED_VOLT(PD_MIN_MV) |
				PDO_FIXED_CURR(vbus[CHG].ma) |
				DUT_PDO_FIXED_FLAGS |
				PDO_FIXED_EXTERNAL;

			chg_pdo_cnt = 1;
		}
	}

	/* Call DUT port manager to update Rp and possible PD contract */
	board_manage_dut_port();
}

int board_set_active_charge_port(int charge_port)
{
	if (charge_port == DUT)
		return -1;

	active_charge_port = charge_port;
	update_ports();

	if (!charge_port_is_active())
		/* Don't negotiate > 5V, except in lockstep with DUT */
		pd_set_external_voltage_limit(CHG, PD_MIN_MV);

	return 0;
}

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	if (port != CHG)
		return;

	active_charge_supplier = supplier;

	/* Update the voltage/current values for CHG port */
	vbus[CHG].ma = charge_ma;
	vbus[CHG].mv = charge_mv;
	update_ports();
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

int pd_adc_read(int port, int cc)
{
	int mv;

	if (port == 0)
		mv = adc_read_channel(cc ? ADC_CHG_CC2_PD : ADC_CHG_CC1_PD);
	else if (!disable_cc)
		mv = adc_read_channel(cc ? ADC_DUT_CC2_PD : ADC_DUT_CC1_PD);
	else {
		/*
		 * When disable_cc, fake the voltage on CC to 0 to avoid
		 * triggering some debounce logic.
		 *
		 * The servo v4 makes Rd/Rp open but the DUT may present Rd/Rp
		 * alternatively that makes the voltage on CC falls into some
		 * unexpected range and triggers the PD state machine switching
		 * between SNK_DISCONNECTED and SNK_DISCONNECTED_DEBOUNCE.
		 */
		mv = 0;
	}

	return mv;
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

/* Shadow what would be in TCPC register state. */
static int rp_value_stored = TYPEC_RP_USB;
static int cc_pull_stored = TYPEC_CC_RD;

int pd_set_rp_rd(int port, int cc_pull, int rp_value)
{
	int rv = EC_SUCCESS;

	if (port != 1)
		return EC_ERROR_UNIMPLEMENTED;

	/* CC is disabled for emulating detach. Don't change Rd/Rp. */
	if (disable_cc)
		return EC_SUCCESS;

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

	rp_value_stored = rp_value;
	cc_pull_stored = cc_pull;

	return rv;
}

int board_select_rp_value(int port, int rp)
{
	if (port != 1)
		return EC_ERROR_UNIMPLEMENTED;

	/*
	 * Update Rp value to indicate non-pd power available.
	 * Do not change pull direction though.
	 */
	if ((rp != rp_value_stored) && (cc_pull_stored == TYPEC_CC_RP)) {
		rp_value_stored = rp;
		return pd_set_rp_rd(port, TYPEC_CC_RP, rp);
	}

	return EC_SUCCESS;
}

int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	int pdo_cnt = 0;

	/*
	 * If CHG is providing VBUS, then advertise what's available on the CHG
	 * port, otherwise we provide no power.
	 */
	if (charge_port_is_active()) {
		*src_pdo =  pd_src_chg_pdo;
		pdo_cnt = chg_pdo_cnt;
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
	timestamp_t deadline;
	uint32_t ma, mv;

	pd_extract_pdo_power(pd_src_chg_pdo[idx - 1], &ma, &mv);
	/* Is this a transition to a new voltage? */
	if (charge_port_is_active() && vbus[CHG].mv != mv) {
		/*
		 * Alter voltage limit on charge port, this should cause
		 * the port to select the desired PDO.
		 */
		pd_set_external_voltage_limit(CHG, mv);

		/* Wait for CHG transition */
		deadline.val = get_time().val + PD_T_PS_TRANSITION;
		CPRINTS("Waiting for CHG port transition");
		while (charge_port_is_active() &&
		       vbus[CHG].mv != mv &&
		       get_time().val < deadline.val)
			msleep(10);

		if (vbus[CHG].mv != mv) {
			CPRINTS("Missed CHG transition, resetting DUT");
			pd_power_supply_reset(DUT);
			return;
		}

		CPRINTS("CHG transitioned");
	}

	vbus[DUT].mv = vbus[CHG].mv;
	vbus[DUT].ma = vbus[CHG].ma;
}

int pd_set_power_supply_ready(int port)
{
	/* Port 0 can never provide vbus. */
	if (port == CHG)
		return EC_ERROR_INVAL;

	if (charge_port_is_active()) {
		/* Enable VBUS */
		gpio_set_level(GPIO_DUT_CHG_EN, 1);

		if (vbus[CHG].mv != PD_MIN_MV)
			CPRINTS("ERROR, CHG port voltage %d != PD_MIN_MV",
				vbus[CHG].mv);

		vbus[DUT].mv = vbus[CHG].mv;
		vbus[DUT].ma = vbus[CHG].mv;
		pd_set_dual_role(DUT, PD_DRP_FORCE_SOURCE);
	} else {
		vbus[DUT].mv = 0;
		vbus[DUT].ma = 0;
		gpio_set_level(GPIO_DUT_CHG_EN, 0);
		pd_set_dual_role(DUT, PD_DRP_FORCE_SINK);
		return EC_ERROR_NOT_POWERED;
	}

	/* Enable CCD, if debuggable TS attached */
	if (pd_ts_dts_plugged(DUT))
		ccd_enable(1);

	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Port 0 can never provide vbus. */
	if (port == CHG)
		return;

	ccd_enable(0);

	/* Disable VBUS */
	gpio_set_level(GPIO_DUT_CHG_EN, 0);

	/* DUT is lost, back to 5V limit on CHG */
	pd_set_external_voltage_limit(CHG, PD_MIN_MV);
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

	/* Port 0 can never provide vbus. */
	if (port == CHG)
		return 0;

	if (pd_snk_is_vbus_provided(CHG))
		return 1;

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
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_DFP &&
	    !disable_dts_mode)
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


static void print_cc_mode(void)
{
	/* Get current CCD status */
	ccprintf("cc: %s\n", disable_cc ? "off" : "on");
	ccprintf("dts mode: %s\n", disable_dts_mode ? "off" : "on");
	ccprintf("chg mode: %s\n",
		pd_get_dual_role(DUT) == PD_DRP_FORCE_SOURCE ?
		"on" : "off");
	ccprintf("chg allowed: %s\n", allow_src_mode ? "on" : "off");
}


static void do_cc(int disable_cc_new, int disable_dts_new, int allow_src_new)
{
	int dualrole;

	if ((disable_cc_new != disable_cc) ||
	    (disable_dts_new != disable_dts_mode) ||
	    (allow_src_new != allow_src_mode)) {
		if (!disable_cc) {
			/* Force detach */
			pd_power_supply_reset(DUT);
			/* Always set to 0 here so both CC lines are changed */
			disable_dts_mode = 0;
			allow_src_mode = 0;

			/* Remove Rp/Rd on both CC lines */
			pd_comm_enable(DUT, 0);
			pd_set_rp_rd(DUT, TYPEC_CC_RP, TYPEC_RP_RESERVED);

			/*
			 * If just changing mode (cc keeps enabled), give some
			 * time for DUT to detach, use tErrorRecovery.
			 */
			if (!disable_cc_new)
				usleep(PD_T_ERROR_RECOVERY);
		}

		/* Accept new cc/dts/src value */
		disable_cc = disable_cc_new;
		disable_dts_mode = disable_dts_new;
		allow_src_mode = allow_src_new;

		if (!disable_cc) {
			/* Can we charge? */
			dualrole = allow_src_mode && charge_port_is_active();
			pd_set_dual_role(DUT, dualrole ?
				PD_DRP_FORCE_SOURCE : PD_DRP_FORCE_SINK);

			/*
			 * Present Rp or Rd on CC1 and CC2 based on
			 * disable_dts_mode
			 */
			pd_config_init(DUT, dualrole);
			pd_comm_enable(DUT, dualrole);
		}
	}
}

static int command_cc(int argc, char **argv)
{
	int disable_cc_new;
	int disable_dts_new;
	int allow_src_new;

	if (argc < 2) {
		print_cc_mode();
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "off")) {
		disable_cc_new = 1;
		disable_dts_new = 0;
		allow_src_new = 0;
	} else if (!strcasecmp(argv[1], "src")) {
		disable_cc_new = 0;
		disable_dts_new = 1;
		allow_src_new = 1;
	} else if (!strcasecmp(argv[1], "snk")) {
		disable_cc_new = 0;
		disable_dts_new = 1;
		allow_src_new = 0;
	} else if (!strcasecmp(argv[1], "srcdts")) {
		disable_cc_new = 0;
		disable_dts_new = 0;
		allow_src_new = 1;
	} else if (!strcasecmp(argv[1], "snkdts")) {
		disable_cc_new = 0;
		disable_dts_new = 0;
		allow_src_new = 0;
	} else {
		ccprintf("Try one of off, src, snk, srcdts, snkdts\n");
		return EC_ERROR_PARAM2;
	}
	do_cc(disable_cc_new, disable_dts_new, allow_src_new);
	print_cc_mode();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cc, command_cc,
			"off|src|snk|srcdts|snkdts",
			"Servo_v4 DTS and CHG mode");

static void fake_disconnect_end(void)
{
	/* Reenable CC lines with previous dts and src modes */
	do_cc(0, disable_dts_mode, allow_src_mode);
}
DECLARE_DEFERRED(fake_disconnect_end);

static void fake_disconnect_start(void)
{
	/* Disable CC lines */
	do_cc(1, disable_dts_mode, allow_src_mode);

	hook_call_deferred(&fake_disconnect_end_data,
			   fake_pd_disconnect_duration_us);
}
DECLARE_DEFERRED(fake_disconnect_start);

static int cmd_fake_disconnect(int argc, char *argv[])
{
	int delay_ms, duration_ms;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	delay_ms = strtoi(argv[1], &e, 0);
	if (*e || delay_ms < 0)
		return EC_ERROR_PARAM1;
	duration_ms = strtoi(argv[2], &e, 0);
	if (*e || duration_ms < 0)
		return EC_ERROR_PARAM2;

	/* Cancel any pending function calls */
	hook_call_deferred(&fake_disconnect_start_data, -1);
	hook_call_deferred(&fake_disconnect_end_data, -1);

	fake_pd_disconnect_duration_us = duration_ms * MSEC;
	hook_call_deferred(&fake_disconnect_start_data, delay_ms * MSEC);

	ccprintf("Fake disconnect for %d ms starting in %d ms.\n",
		duration_ms, delay_ms);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fakedisconnect, cmd_fake_disconnect,
			"<delay_ms> <duration_ms>", NULL);
