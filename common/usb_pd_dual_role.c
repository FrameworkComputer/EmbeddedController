/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dual Role (Source & Sink) USB-PD module.
 */

#include "charge_state.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#if defined(PD_MAX_VOLTAGE_MV) && defined(PD_OPERATING_POWER_MW)
/*
 * As a sink, this is the max voltage (in millivolts) we can request
 * before getting source caps
 */
static unsigned int max_request_mv = PD_MAX_VOLTAGE_MV;

STATIC_IF_NOT(CONFIG_USB_PD_PREFER_MV)
struct pd_pref_config_t __maybe_unused pd_pref_config;

void pd_set_max_voltage(unsigned int mv)
{
	max_request_mv = mv;
}

unsigned int pd_get_max_voltage(void)
{
	return max_request_mv;
}

/*
 * Zinger implements a board specific usb policy that does not define
 * PD_MAX_VOLTAGE_MV and PD_OPERATING_POWER_MW. And in turn, does not
 * use the following functions.
 */
int pd_find_pdo_index(uint32_t src_cap_cnt, const uint32_t * const src_caps,
					int max_mv, uint32_t *selected_pdo)
{
	int i, uw, mv;
	int ret = 0;
	int cur_uw = 0;
	int has_preferred_pdo;
	int prefer_cur;
	int desired_uw = 0;
	const int prefer_mv = pd_pref_config.mv;
	const int type = pd_pref_config.type;

	int __attribute__((unused)) cur_mv = 0;

	if (IS_ENABLED(CONFIG_USB_PD_PREFER_MV))
		desired_uw = charge_get_plt_plus_bat_desired_mw() * 1000;

	/* max voltage is always limited by this boards max request */
	max_mv = MIN(max_mv, PD_MAX_VOLTAGE_MV);

	/* Get max power that is under our max voltage input */
	for (i = 0; i < src_cap_cnt; i++) {
		/* its an unsupported Augmented PDO (PD3.0) */
		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_AUGMENTED)
			continue;

		mv = ((src_caps[i] >> 10) & 0x3FF) * 50;
		/* Skip invalid voltage */
		if (!mv)
			continue;
		/* Skip any voltage not supported by this board */
		if (!pd_is_valid_input_voltage(mv))
			continue;

		if ((src_caps[i] & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
			uw = 250000 * (src_caps[i] & 0x3FF);
		} else {
			int ma = (src_caps[i] & 0x3FF) * 10;

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
		} else if (IS_ENABLED(CONFIG_USB_PD_PREFER_MV)) {
			/* Pick if the PDO provides more than desired. */
			if (uw >= desired_uw) {
				/* pick if cur_uw is less than desired watt */
				if (cur_uw < desired_uw)
					prefer_cur = 1;
				else if (type == PD_PREFER_BUCK) {
					/*
					 * pick the smallest mV above prefer_mv
					 */
					if (mv >= prefer_mv && mv < cur_mv)
						prefer_cur = 1;
					/*
					 * pick if cur_mv is less than
					 * prefer_mv, and we have higher mV
					 */
					else if (cur_mv < prefer_mv &&
						 mv > cur_mv)
						prefer_cur = 1;
				} else if (type == PD_PREFER_BOOST) {
					/*
					 * pick the largest mV below prefer_mv
					 */
					if (mv <= prefer_mv && mv > cur_mv)
						prefer_cur = 1;
					/*
					 * pick if cur_mv is larger than
					 * prefer_mv, and we have lower mV
					 */
					else if (cur_mv > prefer_mv &&
						 mv < cur_mv)
						prefer_cur = 1;
				}
			/*
			 * pick the largest power if we don't see one staisfy
			 * desired power
			 */
			} else if (cur_uw == 0 || uw > cur_uw) {
				prefer_cur = 1;
			}
		}

		/* Prefer higher power, except for tiebreaker */
		has_preferred_pdo =
			prefer_cur ||
			(!IS_ENABLED(CONFIG_USB_PD_PREFER_MV) && uw > cur_uw);

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

void pd_extract_pdo_power(uint32_t pdo, uint32_t *ma, uint32_t *mv)
{
	int max_ma, uw;

	*mv = ((pdo >> 10) & 0x3FF) * 50;

	if (*mv == 0) {
		*ma = 0;
		return;
	}

	if ((pdo & PDO_TYPE_MASK) == PDO_TYPE_BATTERY) {
		uw = 250000 * (pdo & 0x3FF);
		max_ma = 1000 * MIN(1000 * uw, PD_MAX_POWER_MW) / *mv;
	} else {
		max_ma = 10 * (pdo & 0x3FF);
		max_ma = MIN(max_ma, PD_MAX_POWER_MW * 1000 / *mv);
	}

	*ma = MIN(max_ma, PD_MAX_CURRENT_MA);
}

void pd_build_request(uint32_t src_cap_cnt, const uint32_t * const src_caps,
			int32_t vpd_vdo, uint32_t *rdo, uint32_t *ma,
			uint32_t *mv, enum pd_request_type req_type,
			uint32_t max_request_mv, int port)
{
	uint32_t pdo;
	int pdo_index, flags = 0;
	int uw;
	int max_or_min_ma;
	int max_or_min_mw;
	int max_vbus;
	int vpd_vbus_dcr;
	int vpd_gnd_dcr;

	if (req_type == PD_REQUEST_VSAFE5V) {
		/* src cap 0 should be vSafe5V */
		pdo_index = 0;
		pdo = src_caps[0];
	} else {
		/* find pdo index for max voltage we can request */
		pdo_index = pd_find_pdo_index(src_cap_cnt, src_caps,
						max_request_mv, &pdo);
	}

	pd_extract_pdo_power(pdo, ma, mv);

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
	 * If the port partner is capable of USB communication set the
	 * USB Communications Capable flag.
	 * If the port partner is sink device do not suspend USB as the
	 * power can be used for charging.
	 */
	if (pd_get_partner_usb_comm_capable(port)) {
		*rdo |= RDO_COMM_CAP;
		if (pd_get_power_role(port) == PD_ROLE_SINK)
			*rdo |= RDO_NO_SUSPEND;
	}
}
#endif /* defined(PD_MAX_VOLTAGE_MV) && defined(PD_OPERATING_POWER_MW) */

int pd_charge_from_device(uint16_t vid, uint16_t pid)
{
	/* TODO: rewrite into table if we get more of these */
	/*
	 * White-list Apple charge-through accessory since it doesn't set
	 * unconstrained bit, but we still need to charge from it when
	 * we are a sink.
	 */
	return (vid == USB_VID_APPLE &&
		(pid == USB_PID1_APPLE || pid == USB_PID2_APPLE));
}
