/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dual Role (Source & Sink) USB-PD module.
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "dps.h"
#include "system.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)

#if defined(PD_MAX_VOLTAGE_MV) && defined(PD_OPERATING_POWER_MW)
/*
 * As a sink, this is the max voltage (in millivolts) we can request
 * before getting source caps
 */
static unsigned int max_request_mv = PD_MAX_VOLTAGE_MV;

void pd_set_max_voltage(unsigned int mv)
{
	max_request_mv = mv;
}

unsigned int pd_get_max_voltage(void)
{
	return max_request_mv;
}

/**
 * Return true if port is capable of communication over USB data lines.
 *
 * @param port USB-C port number
 */
static bool pd_get_usb_comm_capable(int port)
{
	uint32_t fixed_pdo;

	/* the fixed PDO is always the first entry */
	if (pd_get_power_role(port) == PD_ROLE_SINK) {
		fixed_pdo = pd_snk_pdo[0];
	} else {
		const uint32_t *pdo;

		pd_get_source_pdo(&pdo, port);
		fixed_pdo = pdo[0];
	}

	return !!(fixed_pdo & PDO_FIXED_COMM_CAP);
}

/*
 * Zinger implements a board specific usb policy that does not define
 * PD_MAX_VOLTAGE_MV and PD_OPERATING_POWER_MW. And in turn, does not
 * use the following functions.
 */
int pd_find_pdo_index(uint32_t src_cap_cnt, const uint32_t *const src_caps,
		      int max_mv, uint32_t *selected_pdo)
{
	int i, uw, mv;
	int ret = 0;
	int cur_uw = 0;
	int has_preferred_pdo;
	int prefer_cur;
	int __attribute__((unused)) cur_mv = 0;

	/* max voltage is always limited by this boards max request */
	max_mv = MIN(max_mv, PD_MAX_VOLTAGE_MV);

	/* Get max power that is under our max voltage input */
	for (i = 0; i < src_cap_cnt; i++) {
		if (IS_ENABLED(CONFIG_USB_PD_ONLY_FIXED_PDOS) &&
		    (src_caps[i] & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
			continue;
		/* its an unsupported Augmented PDO (PD3.0) */
		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED)
			continue;

		mv = PDO_FIXED_GET_VOLT(src_caps[i]);
		/* Skip invalid voltage */
		if (!mv)
			continue;
		/*
		 * It's illegal to have EPR PDO in 1...7.
		 * TODO: This is supposed to be a hard reset (8.3.3.3.8)
		 */
		if (i < 7 && mv > PD_MAX_SPR_VOLTAGE)
			continue;
		/* Skip any voltage not supported by this board */
		if (!pd_is_valid_input_voltage(mv))
			continue;

		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
			uw = 250000 * (src_caps[i] & 0x3FF);
		} else {
			int ma = PDO_FIXED_GET_CURR(src_caps[i]);

			ma = MIN(ma, PD_MAX_CURRENT_MA);
			uw = ma * mv;
		}

		if (mv > max_mv)
			continue;
		uw = MIN(uw, PD_MAX_POWER_MW * 1000);
		prefer_cur = 0;

		/* Apply special rules in favor of voltage  */
		if (IS_ENABLED(PD_PREFER_LOW_VOLTAGE)) {
			if (uw == cur_uw && mv < cur_mv)
				prefer_cur = 1;
		} else if (IS_ENABLED(PD_PREFER_HIGH_VOLTAGE)) {
			if (uw == cur_uw && mv > cur_mv)
				prefer_cur = 1;
		}

		/* Prefer higher power, except for tiebreaker */
		has_preferred_pdo = prefer_cur || (uw > cur_uw);

		if (has_preferred_pdo) {
			ret = i;
			cur_uw = uw;
			cur_mv = mv;
		}
	}

	if (selected_pdo)
		*selected_pdo = src_caps[ret];

	return ret;
}

void pd_build_request(int32_t vpd_vdo, uint32_t *rdo, uint32_t *ma,
		      uint32_t *mv, int port)
{
	uint32_t pdo;
	int pdo_index, flags = 0;
	int uw;
	int max_or_min_ma;
	int max_or_min_mw;
	int max_vbus;
	int vpd_vbus_dcr;
	int vpd_gnd_dcr;
	uint32_t src_cap_cnt = pd_get_src_cap_cnt(port);
	const uint32_t *const src_caps = pd_get_src_caps(port);
	int charging_allowed;
	int max_request_allowed;
	uint32_t max_request_mv = pd_get_max_voltage();
	uint32_t unused;

	/*
	 * If this port is the current charge port, or if there isn't an active
	 * charge port, set this value to true. If CHARGE_PORT_NONE isn't
	 * considered, then there can be a race condition in PD negotiation and
	 * the charge manager which forces an incorrect request for
	 * vSafe5V. This can then lead to a brownout condition when the input
	 * current limit gets incorrectly set to 0.5A.
	 */
	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		int chg_port = charge_manager_get_selected_charge_port();

		charging_allowed =
			(chg_port == port || chg_port == CHARGE_PORT_NONE);
	} else {
		charging_allowed = 1;
	}

	if (IS_ENABLED(CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED))
		max_request_allowed = pd_is_max_request_allowed();
	else
		max_request_allowed = 1;

	if (IS_ENABLED(CONFIG_USB_PD_DPS) && dps_is_enabled())
		max_request_mv = MIN(max_request_mv, dps_get_dynamic_voltage());

	/*
	 * If currently charging on a different port, or we are not allowed to
	 * request the max voltage, then select vSafe5V
	 */
	if (charging_allowed && max_request_allowed) {
		/* find pdo index for max voltage we can request */
		pdo_index = pd_find_pdo_index(src_cap_cnt, src_caps,
					      max_request_mv, &pdo);
	} else {
		/* src cap 0 should be vSafe5V */
		pdo_index = 0;
		pdo = src_caps[0];
	}

	pd_extract_pdo_power(pdo, ma, mv, &unused);

	/*
	 * Adjust VBUS current if CTVPD device was detected.
	 */
	if (vpd_vdo > 0) {
		max_vbus = VPD_VDO_MAX_VBUS(vpd_vdo);
		vpd_vbus_dcr = VPD_VDO_VBUS_IMP(vpd_vdo) << 1;
		vpd_gnd_dcr = VPD_VDO_GND_IMP(vpd_vdo);

		/*
		 * Valid max_vbus values:
		 *   00b - 20000 mV
		 *   01b - 30000 mV
		 *   10b - 40000 mV
		 *   11b - 50000 mV
		 */
		max_vbus = 20000 + max_vbus * 10000;
		if (*mv > max_vbus)
			*mv = max_vbus;

		/*
		 * 5000 mA cable: 150 = 750000 / 50000
		 * 3000 mA cable: 250 = 750000 / 30000
		 */
		if (*ma > 3000)
			*ma = 750000 / (150 + vpd_vbus_dcr + vpd_gnd_dcr);
		else
			*ma = 750000 / (250 + vpd_vbus_dcr + vpd_gnd_dcr);
	}

	uw = *ma * *mv;
	/* Mismatch bit set if less power offered than the operating power */
	if (uw < (1000 * PD_OPERATING_POWER_MW))
		flags |= RDO_CAP_MISMATCH;

	/* b:271612382S has more details. */
	if (IS_ENABLED(CONFIG_USB_PD_EPR))
		flags |= RDO_EPR_MODE_CAPABLE;

#ifdef CONFIG_USB_PD_GIVE_BACK
	/* Tell source we are give back capable. */
	flags |= RDO_GIVE_BACK;

	/*
	 * BATTERY PDO: Inform the source that the sink will reduce
	 * power to this minimum level on receipt of a GotoMin Request.
	 */
	max_or_min_mw = PD_MIN_POWER_MW;

	/*
	 * FIXED or VARIABLE PDO: Inform the source that the sink will
	 * reduce current to this minimum level on receipt of a GotoMin
	 * Request.
	 */
	max_or_min_ma = PD_MIN_CURRENT_MA;
#else
	/*
	 * Can't give back, so set maximum current and power to
	 * operating level.
	 */
	max_or_min_ma = *ma;
	max_or_min_mw = uw / 1000;
#endif

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		int mw = uw / 1000;
		*rdo = RDO_BATT(pdo_index + 1, mw, max_or_min_mw, flags);
	} else {
		*rdo = RDO_FIXED(pdo_index + 1, *ma, max_or_min_ma, flags);
	}

	/*
	 * Ref: USB Power Delivery Specification
	 * (Revision 3.0, Version 2.0 / Revision 2.0, Version 1.3)
	 * 6.4.2.4 USB Communications Capable
	 * 6.4.2.5 No USB Suspend
	 *
	 * If the port is capable of USB communication, set the USB
	 * Communications Capable flag.
	 * If the port partner is sink device do not suspend USB as the
	 * power can be used for charging.
	 */
	if (pd_get_usb_comm_capable(port)) {
		*rdo |= RDO_COMM_CAP;
		if (pd_get_power_role(port) == PD_ROLE_SINK)
			*rdo |= RDO_NO_SUSPEND;
	}
}

void pd_process_source_cap(int port, int cnt, uint32_t *src_caps)
{
	pd_set_src_caps(port, cnt, src_caps);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		uint32_t ma, mv, pdo, unused;
		uint32_t max_mv = pd_get_max_voltage();

		if (IS_ENABLED(CONFIG_USB_PD_DPS) && dps_is_enabled())
			max_mv = MIN(max_mv, dps_get_dynamic_voltage());

		/* Get max power info that we could request */
		pd_find_pdo_index(pd_get_src_cap_cnt(port),
				  pd_get_src_caps(port), max_mv, &pdo);
		pd_extract_pdo_power(pdo, &ma, &mv, &unused);

		/* Set max. limit, but 2.5 W ceiling will be applied later. */
		pd_set_input_current_limit(port, ma, mv);
	}
}
#endif /* defined(PD_MAX_VOLTAGE_MV) && defined(PD_OPERATING_POWER_MW) */

bool pd_is_battery_capable(void)
{
	bool capable;

	/* Battery is present and at some minimum percentage. */
	capable = (usb_get_battery_soc() >= CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC);

#ifdef CONFIG_BATTERY_REVIVE_DISCONNECT
	/*
	 * Not capable if the battery is in the disconnect state. The discharge
	 * FET may not be enabled and so attempting being a SRC may cut off
	 * our only power source at the time.
	 */
	capable &= (battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED);
#elif defined(CONFIG_BATTERY_PRESENT_CUSTOM) || \
	defined(CONFIG_BATTERY_PRESENT_GPIO)
	/*
	 * When battery is cutoff in ship mode it may not be reliable to
	 * check if battery is present with its state of charge.
	 * Also check if battery is initialized and ready to provide power.
	 */
	capable &= (battery_is_present() == BP_YES);
#endif /* CONFIG_BATTERY_PRESENT_[CUSTOM|GPIO] */

	return capable;
}

#ifdef CONFIG_USB_PD_TRY_SRC
bool pd_is_try_source_capable(void)
{
	int i;
	uint8_t try_src = 0;
	bool new_try_src;

	for (i = 0; i < board_get_usb_pd_port_count(); i++)
		try_src |= (pd_get_dual_role(i) == PD_DRP_TOGGLE_ON);

	/*
	 * Enable try source when dual-role toggling AND battery is capable
	 * of powering the whole system.
	 */
	new_try_src = (try_src && pd_is_battery_capable());

#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
	/*
	 * If a dedicated supplier is present, power is not a concern and
	 * therefore allow Try.Src if we're toggling.
	 */
	new_try_src = try_src && (charge_manager_get_supplier() ==
				  CHARGE_SUPPLIER_DEDICATED);
#endif /* CONFIG_DEDICATED_CHARGE_PORT_COUNT */

	return new_try_src;
}
#endif /* CONFIG_USB_PD_TRY_SRC */

static int get_bbram_idx(uint8_t port)
{
	if (port < MAX_SYSTEM_BBRAM_IDX_PD_PORTS)
		return (port + SYSTEM_BBRAM_IDX_PD0);

	return -1;
}

int pd_get_saved_port_flags(int port, uint8_t *flags)
{
	if (system_get_bbram(get_bbram_idx(port), flags) != EC_SUCCESS) {
#ifndef CHIP_HOST
		CPRINTF("PD NVRAM FAIL");
#endif
		return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
}

static void pd_set_saved_port_flags(int port, uint8_t flags)
{
	if (system_set_bbram(get_bbram_idx(port), flags) != EC_SUCCESS) {
#ifndef CHIP_HOST
		CPRINTF("PD NVRAM FAIL");
#endif
	}
}

void pd_update_saved_port_flags(int port, uint8_t flag, uint8_t do_set)
{
	uint8_t saved_flags;

	if (pd_get_saved_port_flags(port, &saved_flags) != EC_SUCCESS)
		return;

	if (do_set)
		saved_flags |= flag;
	else
		saved_flags &= ~flag;

	pd_set_saved_port_flags(port, saved_flags);
}
