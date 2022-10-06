/* Copyright 2021 The ChromiumOS Authors
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

#define RT1718S_SW_RESET_DELAY_MS 2
/* Time for delay deasserting EN_FRS after FRS VBUS drop. */
#define RT1718S_FRS_DIS_DELAY (5 * MSEC)

#define FLAG_FRS_ENABLED BIT(0)
#define FLAG_FRS_RX_SIGNALLED BIT(1)
#define FLAG_FRS_VBUS_VALID_FALL BIT(2)
static atomic_t frs_flag[CONFIG_USB_PD_PORT_MAX_COUNT];

/* i2c_write function which won't wake TCPC from low power mode. */
static int rt1718s_write(int port, int reg, int val, int len)
{
	if (reg > 0xFF) {
		return i2c_write_offset16(tcpc_config[port].i2c_info.port,
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
		return i2c_read_offset16(tcpc_config[port].i2c_info.port,
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

int rt1718s_sw_reset(int port)
{
	int rv;

	rv = rt1718s_update_bits8(port, RT1718S_SYS_CTRL3, RT1718S_SWRESET_MASK,
				  0xFF);

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

static int rt1718s_workaround(int port)
{
	int device_id;

	RETURN_ERROR(tcpc_read16(port, RT1718S_DEVICE_ID, &device_id));

	switch (device_id) {
	case RT1718S_DEVICE_ID_ES1:
		RETURN_ERROR(rt1718s_update_bits8(
			port, RT1718S_VCONN_CONTROL_3,
			RT1718S_VCONN_CONTROL_3_VCONN_OVP_DEG, 0xFF));
		/* fallthrough */
	case RT1718S_DEVICE_ID_ES2:
		RETURN_ERROR(rt1718s_update_bits8(
			port, TCPC_REG_FAULT_CTRL,
			TCPC_REG_FAULT_CTRL_VBUS_OCP_FAULT_DIS, 0xFF));
		RETURN_ERROR(rt1718s_update_bits8(
			port, RT1718S_VCON_CTRL4,
			RT1718S_VCON_CTRL4_UVP_CP_EN |
				RT1718S_VCON_CTRL4_OCP_CP_EN,
			0));
		RETURN_ERROR(rt1718s_update_bits8(
			port, RT1718S_VCONN_CONTROL_2,
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

static int rt1718s_set_vconn(int port, int enable)
{
	if (enable) {
		/*
		 * b/233698718#comment9: The initial output spike will be likely
		 * trigger the Vconn OCP. Workaround this by disabling the OCP
		 * at the beginning of sourcing Vconn, and then enable OCP back
		 * after Vconn sourced.
		 */
		RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_VCON_CTRL3,
						  RT1718S_VCON_LIMIT_MODE,
						  0xFF));

		/* Enable Vconn RVP */
		RETURN_ERROR(rt1718s_update_bits8(
			port, RT1718S_VCONN_CONTROL_2,
			RT1718S_VCONN_CONTROL_2_RVP_EN, 0xFF));
	}

	RETURN_ERROR(tcpci_tcpm_set_vconn(port, enable));

	if (enable) {
		/* It takes 10ms that we can switch back to shutdown mode. */
		msleep(10);
		RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_VCON_CTRL3,
						  RT1718S_VCON_LIMIT_MODE, 0));
	} else {
		/* Disable Vconn RVP */
		RETURN_ERROR(rt1718s_update_bits8(
			port, RT1718S_VCONN_CONTROL_2,
			RT1718S_VCONN_CONTROL_2_RVP_EN, 0x0));
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

	/* Set VCONN_OCP_SEL to 400mA */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_VCONN_CONTROL_3,
					  RT1718S_VCONN_CONTROL_3_VCONN_OCP_SEL,
					  0x7F));

	/* Increase the Vconn OCP shoot detection from 200ns to 3~5us */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_VCON_CTRL4,
					  RT1718S_VCON_CTRL4_OCP_CP_EN, 0));

	/* Disable FOD function */
	RETURN_ERROR(rt1718s_update_bits8(port, 0xCF, 0x40, 0x00));

	/* Tcpc connect invalid disabled. Exit shipping mode */
	RETURN_ERROR(rt1718s_update_bits8(port, RT1718S_SYS_CTRL1,
					  RT1718S_SYS_CTRL1_TCPC_CONN_INVALID,
					  0x00));
	RETURN_ERROR(rt1718s_update_bits8(
		port, RT1718S_SYS_CTRL1, RT1718S_SYS_CTRL1_SHIPPING_OFF, 0xFF));

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
				   TCPC_REG_ALERT_MASK_VENDOR_DEF, MASK_SET));

	if (IS_ENABLED(CONFIG_USB_PD_FRS)) {
		memset(frs_flag, 0,
		       sizeof(atomic_t) * CONFIG_USB_PD_PORT_MAX_COUNT);
		/* Set Rx frs and valid vbus fall unmasked */
		RETURN_ERROR(rt1718s_update_bits8(
			port, RT1718S_RT_MASK1,
			RT1718S_RT_MASK1_M_RX_FRS |
				RT1718S_RT_MASK1_M_VBUS_FRS_LOW,
			0xFF));
	}

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

static void frs_gpio_disable_deferred(void)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); ++i) {
		if (frs_flag[i] & FLAG_FRS_VBUS_VALID_FALL) {
			atomic_clear_bits(&frs_flag[i],
					  FLAG_FRS_RX_SIGNALLED |
						  FLAG_FRS_VBUS_VALID_FALL);
			/* If the FRS gets enabled again, do not disable it. */
			if (!(frs_flag[i] & FLAG_FRS_ENABLED))
				board_rt1718s_set_frs_enable(i, 0);
		}
	}
}
DECLARE_DEFERRED(frs_gpio_disable_deferred);

void rt1718s_vendor_defined_alert(int port)
{
	int rv, value;

	if (IS_ENABLED(CONFIG_USB_PD_FRS)) {
		int int1;

		rv = rt1718s_read8(port, RT1718S_RT_INT1, &int1);
		if (rv)
			return;
		rv = rt1718s_write8(port, RT1718S_RT_INT1, int1);
		if (rv)
			return;

		if ((int1 & RT1718S_RT_INT1_INT_RX_FRS) &&
		    frs_flag[port] & FLAG_FRS_ENABLED) {
			/*
			 * 1. Sometimes we get Rx signalled even if the
			 * FRS is disabled, so filter it.
			 * 2. Only call pd_got_frs_signal when this is the first
			 * Rx interrupt for this FRS swap, and the FRS is
			 * enabled.  The Rx interrupt may re-send when the
			 * sink voltage is 5V, and this will make us re-entry
			 * the FRS states.
			 * 3. When a FRS hub detached, RT1718S will
			 * raise FRS RX alert as well. In this case,
			 * we are unable to audit the errors in time,
			 * we will still enter the FRS AMS, but it will
			 * fail eventually, and back to CC open state.
			 */
			if (!(frs_flag[port] & FLAG_FRS_RX_SIGNALLED)) {
				atomic_or(&frs_flag[port],
					  FLAG_FRS_RX_SIGNALLED);
				/* notify TCPM we got FRS signal */
				pd_got_frs_signal(port);
			}
		}

		if ((int1 & RT1718S_RT_INT1_INT_VBUS_FRS_LOW)) {
			/*
			 * Only process if have had rx signalled.
			 * VBUS_FRS_LOW alert could be raised multiple times
			 * if VBUS 5V is glitched.
			 */
			if ((frs_flag[port] & FLAG_FRS_RX_SIGNALLED) &&
			    !(frs_flag[port] & FLAG_FRS_VBUS_VALID_FALL)) {
				atomic_or(&frs_flag[port],
					  FLAG_FRS_VBUS_VALID_FALL);
				/*
				 * b/223086905:comment8&comment17
				 * We deferred the FRS disable
				 * (called to rt1718s_set_frs_enable()), now we
				 * can disable it after the VBUS fell.
				 */
				rt1718s_set_frs_enable(port, 0);
				/*
				 * b/228422539:comment4
				 * PPC HL5099 (pin-compatible to NX20P3483)
				 * suggested FRS gpio should be disabled after
				 * the SRC gpio enabled for 5ms to prevent the
				 * PPC from stopping sourcing the VBUS.
				 * Thought this is a workaround for HL5099, but
				 * it shouldn't affect other PPC chips since
				 * the DUT started sourcing the partner already.
				 */
				hook_call_deferred(
					&frs_gpio_disable_deferred_data,
					RT1718S_FRS_DIS_DELAY);
			}
		}

		/* ignore other interrupts for faster frs handling */
		if (int1 & (RT1718S_RT_INT1_INT_RX_FRS |
			    RT1718S_RT_INT1_INT_VBUS_FRS_LOW)) {
			tcpc_write16(port, TCPC_REG_ALERT,
				     TCPC_REG_ALERT_VENDOR_DEF);
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
		usb_charger_task_set_event(port, USB_CHG_EVENT_BC12);

	/* clear the alerts from rt1718s_workaround() */
	rv = rt1718s_write8(port, RT1718S_RT_INT2, 0xFF);
	if (rv)
		return;
	/* ES1 workaround: disable Vconn discharge */
	rv = rt1718s_update_bits8(port, RT1718S_SYS_CTRL2,
				  RT1718S_SYS_CTRL2_VCONN_DISCHARGE_EN, 0);
	if (rv)
		return;

	tcpc_write16(port, TCPC_REG_ALERT, TCPC_REG_ALERT_VENDOR_DEF);
}

__overridable int board_rt1718s_set_snk_enable(int port, int enable)
{
	return EC_SUCCESS;
}

static int rt1718s_tcpm_set_snk_ctrl(int port, int enable)
{
	int rv;

	rv = board_rt1718s_set_snk_enable(port, enable);
	if (rv)
		return rv;

	return tcpci_tcpm_set_snk_ctrl(port, enable);
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

#ifdef CONFIG_USB_PD_FRS

__overridable int board_rt1718s_set_frs_enable(int port, int enable)
{
	return EC_SUCCESS;
}

int rt1718s_set_frs_enable(int port, int enable)
{
	/*
	 * Use write instead of update to save 2 i2c read.
	 * Assume other bits are at their reset value.
	 */
	int frs_ctrl2 = 0x10, vbus_ctrl_en = 0x3F;

	if (enable) {
		atomic_or(&frs_flag[port], FLAG_FRS_ENABLED);
		frs_ctrl2 |= RT1718S_FRS_CTRL2_RX_FRS_EN;
		frs_ctrl2 |= RT1718S_FRS_CTRL2_VBUS_FRS_EN;

		vbus_ctrl_en |= RT1718S_VBUS_CTRL_EN_GPIO2_VBUS_PATH_EN;
		vbus_ctrl_en |= RT1718S_VBUS_CTRL_EN_GPIO1_VBUS_PATH_EN;
	} else {
		atomic_clear_bits(&frs_flag[port], FLAG_FRS_ENABLED);
		if (FLAG_FRS_RX_SIGNALLED ==
		    (frs_flag[port] &
		     (FLAG_FRS_RX_SIGNALLED | FLAG_FRS_VBUS_VALID_FALL))) {
			/*
			 * Skip disable if we had only FRS_RX_SIGNALLED, and
			 * deferred the FRS register disable process in
			 * rt1718s_vendor_defined_alert.
			 */
			return EC_SUCCESS;
		}
	}

	RETURN_ERROR(rt1718s_write8(port, RT1718S_FRS_CTRL2, frs_ctrl2));
	RETURN_ERROR(rt1718s_write8(port, RT1718S_VBUS_CTRL_EN, vbus_ctrl_en));

	/*
	 * b/223086905#comment13, b/228422539:comment4
	 * If this function gets called when FRS RX signalled, then
	 * we'll deferred the GPIO disabled until the VBUS valid drop. So
	 * don't disable it here.
	 */
	if (enable || !(frs_flag[port] & FLAG_FRS_RX_SIGNALLED))
		RETURN_ERROR(board_rt1718s_set_frs_enable(port, enable));

	return EC_SUCCESS;
}
#endif

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
			     RT1718S_GPIO_CTRL_O, value ? 0xFF : 0);
}

int rt1718s_gpio_get_level(int port, enum rt1718s_gpio signal)
{
	int val;

	rt1718s_read8(port, RT1718S_GPIO_CTRL(signal), &val);
	return !!(val & RT1718S_GPIO_CTRL_I);
}

static int command_rt1718s_gpio(int argc, const char **argv)
{
	int i, j;
	uint32_t flags;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (tcpc_config[i].drv != &rt1718s_tcpm_drv)
			continue;

		for (j = 0; j < RT1718S_GPIO_COUNT; j++) {
			int rv;

			rv = rt1718s_read8(i, RT1718S_GPIO_CTRL(j), &flags);
			if (rv)
				return EC_ERROR_UNKNOWN;

			ccprintf("C%d GPIO%d OD=%d PU=%d PD=%d OE=%d HL=%d\n",
				 i, j + 1, !(flags & RT1718S_GPIO_CTRL_OD_N),
				 !!(flags & RT1718S_GPIO_CTRL_PU),
				 !!(flags & RT1718S_GPIO_CTRL_PD),
				 !!(flags & RT1718S_GPIO_CTRL_OE),
				 !!(flags & RT1718S_GPIO_CTRL_O));
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(rt1718s_gpio, command_rt1718s_gpio, "", "RT1718S GPIO");

#ifdef CONFIG_USB_PD_TCPM_SBU
static int rt1718s_set_sbu(int port, bool enable)
{
	/*
	 * The `enable` here means to enable the SBU line (set 1)
	 * - true: connect SBU lines from outer to the host
	 * - false: isolate the SBU lines
	 */
	return rt1718s_update_bits8(port, RT1718S_RT2_SBU_CTRL_01,
				    RT1718S_RT2_SBU_CTRL_01_SBU_VIEN |
					    RT1718S_RT2_SBU_CTRL_01_SBU1_SWEN |
					    RT1718S_RT2_SBU_CTRL_01_SBU2_SWEN,
				    enable ? 0xFF : 0);
}
#endif

/* RT1718S is a TCPCI compatible port controller */
const struct tcpm_drv rt1718s_tcpm_drv = {
	.init = &rt1718s_init,
	.release = &tcpci_tcpm_release,
	.get_cc = &tcpci_tcpm_get_cc,
#ifdef CONFIG_USB_PD_VBUS_DETECT_TCPC
	.check_vbus_level = &tcpci_tcpm_check_vbus_level,
#endif
	.select_rp_value = &tcpci_tcpm_select_rp_value,
	.set_cc = &tcpci_tcpm_set_cc,
	.set_polarity = &tcpci_tcpm_set_polarity,
#ifdef CONFIG_USB_PD_DECODE_SOP
	.sop_prime_enable = &tcpci_tcpm_sop_prime_enable,
#endif
	.set_vconn = &rt1718s_set_vconn,
	.set_msg_header = &tcpci_tcpm_set_msg_header,
	.set_rx_enable = &tcpci_tcpm_set_rx_enable,
	.get_message_raw = &tcpci_tcpm_get_message_raw,
	.transmit = &tcpci_tcpm_transmit,
	.tcpc_alert = &rt1718s_alert,
#ifdef CONFIG_USB_PD_DISCHARGE_TCPC
	.tcpc_discharge_vbus = &tcpci_tcpc_discharge_vbus,
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = &tcpci_tcpc_drp_toggle,
#endif
	.get_chip_info = &tcpci_get_chip_info,
	.set_snk_ctrl = &rt1718s_tcpm_set_snk_ctrl,
	.set_src_ctrl = &tcpci_tcpm_set_src_ctrl,
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &rt1718s_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_PD_FRS_TCPC
	.set_frs_enable = &rt1718s_set_frs_enable,
#endif
	.set_bist_test_mode = &tcpci_set_bist_test_mode,
	.get_bist_test_mode = &tcpci_get_bist_test_mode,
#ifdef CONFIG_USB_PD_TCPM_SBU
	.set_sbu = &rt1718s_set_sbu,
#endif
};

const struct bc12_drv rt1718s_bc12_drv = {
	.usb_charger_task_init = rt1718s_bc12_usb_charger_task_init,
	.usb_charger_task_event = rt1718s_bc12_usb_charger_task_event,
};
