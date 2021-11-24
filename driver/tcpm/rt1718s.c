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
static int rt1718s_write(int port, int reg, int val, int len)
{
	if (reg > 0xFF) {
		return i2c_write_offset16(
			tcpc_config[port].i2c_info.port,
			tcpc_config[port].i2c_info.addr_flags,
			reg, val, len);
	} else if (len == 1) {
		return tcpc_write(port, reg, val);
	} else {
		return tcpc_write16(port, reg, val);
	}
}

static int rt1718s_read(int port, int reg, int *val, int len)
{
	if (reg > 0xFF) {
		return i2c_read_offset16(
			tcpc_config[port].i2c_info.port,
			tcpc_config[port].i2c_info.addr_flags,
			reg, val, len);
	} else if (len == 1) {
		return tcpc_read(port, reg, val);
	} else {
		return tcpc_read16(port, reg, val);
	}
}

int rt1718s_write8(int port, int reg, int val)
{
	return rt1718s_write(port, reg, val, 1);
}

int rt1718s_read8(int port, int reg, int *val)
{
	return rt1718s_read(port, reg, val, 1);
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

int rt1718s_write16(int port, int reg, int val)
{
	return rt1718s_write(port, reg, val, 2);
}

int rt1718s_read16(int port, int reg, int *val)
{
	return rt1718s_read(port, reg, val, 2);
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

	return EC_SUCCESS;
}

static int rt1718s_workaround(int port)
{
	int device_id;

	RETURN_ERROR(tcpc_read16(port, RT1718S_DEVICE_ID, &device_id));

	switch (device_id) {
	case RT1718S_DEVICE_ID_ES1:
		RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_VCONN_CONTROL_3,
					RT1718S_VCONN_CONTROL_3_VCONN_OVP_DEG,
					0xFF));
		/* fallthrough */
	case RT1718S_DEVICE_ID_ES2:
		RETURN_ERROR(rt1718s_update_bits8(port, TCPC_REG_FAULT_CTRL,
					TCPC_REG_FAULT_CTRL_VBUS_OCP_FAULT_DIS,
					0xFF));
		RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_VCON_CTRL4,
					RT1718S_VCON_CTRL4_UVP_CP_EN |
					RT1718S_VCON_CTRL4_OVP_CP_EN,
					0));
		RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_VCONN_CONTROL_2,
					RT1718S_VCONN_CONTROL_2_OVP_EN_CC1 |
					RT1718S_VCONN_CONTROL_2_OVP_EN_CC2,
					0xFF));
		break;
	default:
		/* do nothing */
		break;
	}

	return EC_SUCCESS;
}

static int rt1718s_init(int port)
{
	static bool need_sw_reset = true;

	if (!system_jumped_late() && need_sw_reset) {
		RETURN_ERROR(rt1718s_sw_reset(port));
		need_sw_reset = false;
	}

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

	RETURN_ERROR(rt1718s_workaround(port));
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

static void rt1718s_bc12_usb_charger_task(const int port)
{
	rt1718s_enable_bc12_sink(port, false);

	while (1) {
		uint32_t evt = task_wait_event(-1);

		if (evt & USB_CHG_EVENT_VBUS) {
			bool is_non_pd_sink = !pd_capable(port) &&
				pd_get_power_role(port) == PD_ROLE_SINK &&
				pd_snk_is_vbus_provided(port);

			if (is_non_pd_sink)
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

	if (IS_ENABLED(CONFIG_USB_PD_FRS_PPC) &&
	    IS_ENABLED(CONFIG_USBC_PPC_RT1718S)) {
		int int1;

		rv = rt1718s_read8(port, RT1718S_RT_INT1, &int1);
		if (rv)
			return;
		rv = rt1718s_write8(port, RT1718S_RT_INT1, int1);
		if (rv)
			return;

		if ((int1 & RT1718S_RT_INT1_INT_RX_FRS)) {
			pd_got_frs_signal(port);

			tcpc_write16(port, TCPC_REG_ALERT,
					TCPC_REG_ALERT_VENDOR_DEF);
			/* ignore other interrupts for faster frs handling */
			return;
		}
	}

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

	/* clear the alerts from rt1718s_workaround() */
	rv = rt1718s_write8(port, RT1718S_RT_INT2, 0xFF);
	if (rv)
		return;
	/* ES1 workaround: disable Vconn discharge */
	rv = rt1718s_update_bits8(port, RT1718S_SYS_CTRL2,
			RT1718S_SYS_CTRL2_VCONN_DISCHARGE_EN,
			0);
	if (rv)
		return;

	tcpc_write16(port, TCPC_REG_ALERT, TCPC_REG_ALERT_VENDOR_DEF);
}

static void rt1718s_alert(int port)
{
	int alert;

	tcpc_read16(port, TCPC_REG_ALERT, &alert);
	if (alert & TCPC_REG_ALERT_VENDOR_DEF)
		rt1718s_vendor_defined_alert(port);

	if (alert & ~TCPC_REG_ALERT_VENDOR_DEF)
		tcpci_tcpc_alert(port);
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
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
#endif

int rt1718s_get_adc(int port, enum rt1718s_adc_channel channel, int *adc_val)
{
	static mutex_t adc_lock;
	int rv;
	const int max_wait_times = 30;

	if (in_interrupt_context()) {
		CPRINTS("Err: use ADC in IRQ");
		return EC_ERROR_INVAL;
	}

	mutex_lock(&adc_lock);

	/* Start ADC conversation */
	rv = rt1718s_write16(port, RT1718S_ADC_CTRL_01, BIT(channel));
	if (rv)
		goto out;

	/*
	 * The expected conversion time is 85.3us * number of enabled channels.
	 * Polling for 3ms should be long enough.
	 */
	for (int i = 0; i < max_wait_times; i++) {
		int adc_done;

		usleep(100);
		rv = rt1718s_read8(port, RT1718S_RT_INT6, &adc_done);
		if (rv)
			goto out;
		if (adc_done & RT1718S_RT_INT6_INT_ADC_DONE)
			break;
		if (i == max_wait_times - 1) {
			CPRINTS("conversion fail channel=%d", channel);
			rv = EC_ERROR_TIMEOUT;
			goto out;
		}
	}

	/* Read ADC data */
	rv = rt1718s_read16(port, RT1718S_ADC_CHX_VOL_L(channel), adc_val);
	if (rv)
		goto out;

	/*
	 * The resolution of VBUS1 ADC is 12.5mV,
	 * other channels are 4mV.
	 */
	if (channel == RT1718S_ADC_VBUS1)
		*adc_val = *adc_val * 125 / 10;
	else
		*adc_val *= 4;

out:
	/* Cleanup: disable adc and clear interrupt. Error ignored. */
	rt1718s_write16(port, RT1718S_ADC_CTRL_01, 0);
	rt1718s_write8(port, RT1718S_RT_INT6, RT1718S_RT_INT6_INT_ADC_DONE);

	mutex_unlock(&adc_lock);
	return rv;
}

void rt1718s_gpio_set_flags(int port, enum rt1718s_gpio signal, uint32_t flags)
{
	int val = 0;

	if (!(flags & GPIO_OPEN_DRAIN))
		val |= RT1718S_GPIO_CTRL_OD_N;
	if (flags & GPIO_PULL_UP)
		val |= RT1718S_GPIO_CTRL_PU;
	if (flags & GPIO_PULL_DOWN)
		val |= RT1718S_GPIO_CTRL_PD;
	if (flags & GPIO_HIGH)
		val |= RT1718S_GPIO_CTRL_O;
	if (flags & GPIO_OUTPUT)
		val |= RT1718S_GPIO_CTRL_OE;

	rt1718s_write8(port, RT1718S_GPIO_CTRL(signal), val);
}

void rt1718s_gpio_set_level(int port, enum rt1718s_gpio signal, int value)
{
	rt1718s_update_bits8(port, RT1718S_GPIO_CTRL(signal),
			RT1718S_GPIO_CTRL_O,
			value ? 0xFF : 0);
}

int rt1718s_gpio_get_level(int port, enum rt1718s_gpio signal)
{
	int val;

	rt1718s_read8(port, RT1718S_GPIO_CTRL(signal), &val);
	return !!(val & RT1718S_GPIO_CTRL_I);
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
