/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * RT1718S TCPC Driver
 */

#include "console.h"
#include "driver/tcpm/rt1718s.h"
#include "driver/tcpm/tcpci.h"
#include "driver/tcpm/tcpm.h"
#include "stdint.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define RT1718S_SW_RESET_DELAY_MS 2

/* i2c_write function which won't wake TCPC from low power mode. */
int rt1718s_write8(int port, int reg, int val)
{
	if (reg > 0xFF) {
		return i2c_write_offset16(
			tcpc_config[port].i2c_info.port,
			tcpc_config[port].i2c_info.addr_flags,
			reg, val, 1);
	}
	return tcpc_write(port, reg, val);
}

int rt1718s_read8(int port, int reg, int *val)
{
	if (reg > 0xFF) {
		return i2c_read_offset16(
			tcpc_config[port].i2c_info.port,
			tcpc_config[port].i2c_info.addr_flags,
			reg, val, 1);
	}
	return tcpc_read(port, reg, val);
}

int rt1718s_update_bits8(int port, int reg, int mask, int val)
{
	int reg_val;

	if (mask == 0xFF)
		return rt1718s_write8(port, reg, val);

	RETURN_ERROR(rt1718s_read8(port, reg, &reg_val));

	reg_val &= (~mask);
	reg_val |= (mask & val);
	return rt1718s_write8(port, reg, reg_val);
}

static int rt1718s_sw_reset(int port)
{
	int rv;

	rv = rt1718s_update_bits8(port, RT1718S_SYS_CTRL3,
		RT1718S_SWRESET_MASK, 0xFF);

	msleep(RT1718S_SW_RESET_DELAY_MS);

	return rv;
}

/* enable bc 1.2 sink function  */
static int rt1718s_enable_bc12_sink(int port, bool en)
{
	return rt1718s_update_bits8(port, RT1718S_RT2_BC12_SNK_FUNC,
			RT1718S_RT2_BC12_SNK_FUNC_BC12_SNK_EN,
			en ? 0xFF : 0);
}

static int rt1718s_set_bc12_sink_spec_ta(int port, bool en)
{
	return rt1718s_update_bits8(port,
			RT1718S_RT2_BC12_SNK_FUNC,
			RT1718S_RT2_BC12_SNK_FUNC_SPEC_TA_EN, en ? 0xFF : 0);
}

static int rt1718s_set_bc12_sink_dcdt_sel(int port, uint8_t dcdt_sel)
{
	return rt1718s_update_bits8(port,
			RT1718S_RT2_BC12_SNK_FUNC,
			RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_MASK, dcdt_sel);
}

static int rt1718s_set_bc12_sink_vlgc_option(int port, bool en)
{
	return rt1718s_update_bits8(port,
			RT1718S_RT2_BC12_SNK_FUNC,
			RT1718S_RT2_BC12_SNK_FUNC_VLGC_OPT, en ? 0xFF : 0);
}

static int rt1718s_set_bc12_sink_vport_sel(int port, uint8_t sel)
{
	return rt1718s_update_bits8(port,
			RT1718S_RT2_DPDM_CTR1_DPDM_SET,
			RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_MASK, sel);
}

static int rt1718s_set_bc12_sink_wait_vbus(int port, bool en)
{
	return rt1718s_update_bits8(port,
			RT1718S_RT2_BC12_SNK_FUNC,
			RT1718S_RT2_BC12_SNK_FUNC_BC12_WAIT_VBUS,
			en ? 0xFF : 0);
}

/*
 * rt1718s BC12 function initial
 */
static int rt1718s_bc12_init(int port)
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
	RETURN_ERROR(rt1718s_set_bc12_sink_dcdt_sel(port,
			RT1718S_RT2_BC12_SNK_FUNC_DCDT_SEL_600MS));

	/* Disable vlgc option */
	RETURN_ERROR(rt1718s_set_bc12_sink_vlgc_option(port, false));

	/* DPDM voltage selection */
	RETURN_ERROR(rt1718s_set_bc12_sink_vport_sel(port,
			RT1718S_RT2_DPDM_CTR1_DPDM_SET_DPDM_VSRC_SEL_0_65V));

	/* Disable sink wait vbus */
	RETURN_ERROR(rt1718s_set_bc12_sink_wait_vbus(port, false));

	/* Disable bc 1.2 sink function */
	RETURN_ERROR(rt1718s_enable_bc12_sink(port, false));

	return EC_SUCCESS;
}

static int rt1718s_init(int port)
{
	static bool need_sw_reset = true;

	if (!system_jumped_late() && need_sw_reset) {
		RETURN_ERROR(rt1718s_sw_reset(port));
		need_sw_reset = false;
	}

	if (IS_ENABLED(CONFIG_USB_PD_FRS_TCPC))
		/* Set vbus frs low unmasked, Rx frs unmasked */
		RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_RT_MASK1,
					RT1718S_RT_MASK1_M_VBUS_FRS_LOW |
					RT1718S_RT_MASK1_M_RX_FRS,
					0xFF));


	RETURN_ERROR(rt1718s_bc12_init(port));

	/* Set VBUS_VOL_SEL to 20V */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_RT2_VBUS_VOL_CTRL,
				RT1718S_RT2_VBUS_VOL_CTRL_VOL_SEL,
				RT1718S_VBUS_VOL_TO_REG(20)));

	/* Disable FOD function */
	RETURN_ERROR(rt1718s_update_bits8(port, 0xCF, 0x40, 0x00));

	/* Tcpc connect invalid disabled. Exit shipping mode */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_SYS_CTRL1,
				RT1718S_SYS_CTRL1_TCPC_CONN_INVALID, 0x00));
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_SYS_CTRL1,
				RT1718S_SYS_CTRL1_SHIPPING_OFF, 0xFF));

	/* Clear alert and fault */
	RETURN_ERROR(rt1718s_write8(port, TCPC_REG_FAULT_STATUS, 0xFF));
	RETURN_ERROR(tcpc_write16(port, TCPC_REG_ALERT, 0xFFFF));

	RETURN_ERROR(tcpci_tcpm_init(port));

	/*
	 * Set vendor defined alert unmasked, this must be done after
	 * tcpci_tcpm_init.
	 */
	RETURN_ERROR(tcpc_update16(port, TCPC_REG_ALERT_MASK,
				TCPC_REG_ALERT_MASK_VENDOR_DEF,
				MASK_SET));

	RETURN_ERROR(board_rt1718s_init(port));

	return EC_SUCCESS;
}

__overridable int board_rt1718s_init(int port)
{
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
		charge_manager_update_charge(current_bc12_type, port, NULL);

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

static void rt1718s_bc12_usb_charger_task(const int port)
{
	rt1718s_enable_bc12_sink(port, false);

	while (1) {
		uint32_t evt = task_wait_event(-1);

		if (evt & USB_CHG_EVENT_VBUS) {
			if (pd_snk_is_vbus_provided(port))
				rt1718s_enable_bc12_sink(port, true);
			else
				rt1718s_update_charge_manager(
						port, CHARGE_SUPPLIER_NONE);
		}

		/* detection done, update charge_manager and stop detection */
		if (evt & USB_CHG_EVENT_BC12) {
			int type = rt1718s_get_bc12_type(port);

			rt1718s_update_charge_manager(
					port, type);
			rt1718s_enable_bc12_sink(port, false);
		}
	}
}

void rt1718s_vendor_defined_alert(int port)
{
	int rv, value;

	/* Process BC12 alert */
	rv = rt1718s_read8(port, RT1718S_RT_INT6, &value);
	if (rv)
		return;

	/* clear BC12 alert */
	rv = rt1718s_write8(port, RT1718S_RT_INT6, value);
	if (rv)
		return;

	/* check snk done */
	if (value & RT1718S_RT_INT6_INT_BC12_SNK_DONE)
		task_set_event(USB_CHG_PORT_TO_TASK_ID(port),
			       USB_CHG_EVENT_BC12);
}

static void rt1718s_alert(int port)
{
	int alert;

	tcpc_read16(port, TCPC_REG_ALERT, &alert);
	if (alert & TCPC_REG_ALERT_VENDOR_DEF)
		rt1718s_vendor_defined_alert(port);
	tcpci_tcpc_alert(port);
}

static int rt1718s_enter_low_power_mode(int port)
{
	/* enter low power mode */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_SYS_CTRL2,
					  RT1718S_SYS_CTRL2_LPWR_EN, 0xFF));
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_SYS_CTRL2,
					  RT1718S_SYS_CTRL2_BMCIO_OSC_EN, 0));

	/* disable DP/DM/SBU swtiches */
	RETURN_ERROR(rt1718s_write8(port, RT1718S_RT2_SBU_CTRL_01, 0));

	return tcpci_enter_low_power_mode(port);
}

/* RT1718S is a TCPCI compatible port controller */
const struct tcpm_drv rt1718s_tcpm_drv = {
	.init			= &rt1718s_init,
	.release		= &tcpci_tcpm_release,
	.get_cc			= &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level	= &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value	= &tcpci_tcpm_select_rp_value,
	.set_cc			= &tcpci_tcpm_set_cc,
	.set_polarity		= &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable	= &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn		= &tcpci_tcpm_set_vconn,
	.set_msg_header		= &tcpci_tcpm_set_msg_header,
	.set_rx_enable		= &tcpci_tcpm_set_rx_enable,
	.get_message_raw	= &tcpci_tcpm_get_message_raw,
	.transmit		= &tcpci_tcpm_transmit,
	.tcpc_alert		= &rt1718s_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus	= &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle		= &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info		= &tcpci_get_chip_info,
#ifdef CONFIG_USB_PD_PPC
	.set_snk_ctrl		= &tcpci_tcpm_set_snk_ctrl,
	.set_src_ctrl		= &tcpci_tcpm_set_src_ctrl,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode	= &rt1718s_enter_low_power_mode,
#endif
};

const struct bc12_drv rt1718s_bc12_drv = {
	.usb_charger_task = rt1718s_bc12_usb_charger_task,
};
