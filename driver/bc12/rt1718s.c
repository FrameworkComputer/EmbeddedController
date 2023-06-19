/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * RT1718S BC 1.2 Driver
 */

#include "battery.h"
#include "console.h"
#include "driver/tcpm/rt1718s.h"
#include "gpio.h"
#include "hooks.h"
#include "stdint.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pe_sm.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* enable bc 1.2 sink function  */
static int rt1718s_enable_bc12_sink(int port, bool en)
{
	return rt1718s_update_bits8(port, RT1718S_RT2_BC12_SNK_FUNC,
				    RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN,
				    en ? 0xFF : 0);
}

static int rt1718s_set_bc12_sink_spec_ta(int port, bool en)
{
	return rt1718s_update_bits8(port, RT1718S_RT2_BC12_SNK_FUNC,
				    RT1718S_RT2_BC12_SNK_FUNC_SPEC_TA_EN,
				    en ? 0xFF : 0);
}

static int rt1718s_set_bc12_sink_dcdt_sel(int port, uint8_t dcdt_sel)
{
	return rt1718s_update_bits8(port, RT1718S_RT2_BC12_SNK_FUNC,
				    RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_MASK,
				    dcdt_sel);
}

static int rt1718s_set_bc12_sink_vlgc_option(int port, bool en)
{
	return rt1718s_update_bits8(port, RT1718S_RT2_BC12_SNK_FUNC,
				    RT1718S_RT2_BC12_SNK_FUNC_VLGC_OPT,
				    en ? 0xFF : 0);
}

static int rt1718s_set_bc12_sink_vport_sel(int port, uint8_t sel)
{
	return rt1718s_update_bits8(
		port, RT1718S_RT2_DPDM_CTR1_DPDM_SET,
		RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_MASK, sel);
}

static int rt1718s_set_bc12_sink_wait_vbus(int port, bool en)
{
	return rt1718s_update_bits8(port, RT1718S_RT2_BC12_SNK_FUNC,
				    RT1718S_RT2_BC12_SNK_FUNC_BC12_WAIT_VBUS,
				    en ? 0xFF : 0);
}

/*
 * rt1718s BC12 function initial
 */
int rt1718s_bc12_init(int port)
{
	/* Enable vendor defined BC12 function */
	RETURN_ERROR(rt1718s_write8(port, RT1718S_RT_MASK6,
				    RT1718S_RT_MASK6_M_BC12_SNK_DONE |
					    RT1718S_RT_MASK6_M_BC12_TA_CHG));

	RETURN_ERROR(rt1718s_write8(port, RT1718S_RT2_SBU_CTRL_01,
				    RT1718S_RT2_SBU_CTRL_01_DPDM_VIEN |
					    RT1718S_RT2_SBU_CTRL_01_DM_SWEN |
					    RT1718S_RT2_SBU_CTRL_01_DP_SWEN));

	/* Disable 2.7v mode */
	RETURN_ERROR(rt1718s_set_bc12_sink_spec_ta(port, false));

	/* DCDT select 600ms timeout */
	RETURN_ERROR(rt1718s_set_bc12_sink_dcdt_sel(
		port, RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_600MS));

	/* Disable vlgc option */
	RETURN_ERROR(rt1718s_set_bc12_sink_vlgc_option(port, false));

	/* DPDM voltage selection */
	RETURN_ERROR(rt1718s_set_bc12_sink_vport_sel(
		port, RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_0_65V));

	/* Disable sink wait vbus */
	RETURN_ERROR(rt1718s_set_bc12_sink_wait_vbus(port, false));

	return EC_SUCCESS;
}

static enum charge_supplier rt1718s_get_bc12_type(int port)
{
	int data;

	if (rt1718s_read8(port, RT1718S_RT2_BC12_STAT, &data))
		return CHARGE_SUPPLIER_OTHER;

	switch (data & RT1718S_RT2_BC12_STAT_PORT_STATUS_MASK) {
	case RT1718S_RT2_BC12_STAT_PORT_STATUS_NONE:
		return CHARGE_SUPPLIER_NONE;
	case RT1718S_RT2_BC12_STAT_PORT_STATUS_SDP:
		return CHARGE_SUPPLIER_BC12_SDP;
	case RT1718S_RT2_BC12_STAT_PORT_STATUS_CDP:
		return CHARGE_SUPPLIER_BC12_CDP;
	case RT1718S_RT2_BC12_STAT_PORT_STATUS_DCP:
		return CHARGE_SUPPLIER_BC12_DCP;
	}

	return CHARGE_SUPPLIER_OTHER;
}

static int rt1718s_get_bc12_ilim(enum charge_supplier supplier)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
	case CHARGE_SUPPLIER_BC12_CDP:
		return USB_CHARGER_MAX_CURR_MA;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

static void rt1718s_update_charge_manager(int port,
					  enum charge_supplier new_bc12_type)
{
	static enum charge_supplier current_bc12_type = CHARGE_SUPPLIER_NONE;

	if (new_bc12_type != current_bc12_type) {
		if (current_bc12_type != CHARGE_SUPPLIER_NONE)
			charge_manager_update_charge(current_bc12_type, port,
						     NULL);

		if (new_bc12_type != CHARGE_SUPPLIER_NONE) {
			struct charge_port_info chg = {
				.current = rt1718s_get_bc12_ilim(new_bc12_type),
				.voltage = USB_CHARGER_VOLTAGE_MV,
			};

			charge_manager_update_charge(new_bc12_type, port, &chg);
		}

		current_bc12_type = new_bc12_type;
	}
}

static void rt1718s_bc12_usb_charger_task_init(const int port)
{
	rt1718s_enable_bc12_sink(port, false);
}

static void rt1718s_bc12_usb_charger_task_event(const int port, uint32_t evt)
{
	bool is_non_pd_sink = !pd_capable(port) &&
			      !usb_charger_port_is_sourcing_vbus(port) &&
			      pd_check_vbus_level(port, VBUS_PRESENT);

	if (evt & USB_CHG_EVENT_VBUS) {
		if (is_non_pd_sink)
			rt1718s_enable_bc12_sink(port, true);
		else
			rt1718s_update_charge_manager(port,
						      CHARGE_SUPPLIER_NONE);
	}

	/* detection done, update charge_manager and stop detection */
	if (evt & USB_CHG_EVENT_BC12) {
		int type;

		if (is_non_pd_sink)
			type = rt1718s_get_bc12_type(port);
		else
			type = CHARGE_SUPPLIER_NONE;

		rt1718s_update_charge_manager(port, type);
		rt1718s_enable_bc12_sink(port, false);
	}
}

const struct bc12_drv rt1718s_bc12_drv = {
	.usb_charger_task_init = rt1718s_bc12_usb_charger_task_init,
	.usb_charger_task_event = rt1718s_bc12_usb_charger_task_event,
};
