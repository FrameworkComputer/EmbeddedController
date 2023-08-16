/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/retimer/bb_retimer.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "usb_dp_alt_mode.h"
#include "usb_pd.h"
#include "util.h"

#define BB_RETIMER_REG_SIZE 4
#define BB_RETIMER_READ_SIZE (BB_RETIMER_REG_SIZE + 1)
#define BB_RETIMER_WRITE_SIZE (BB_RETIMER_REG_SIZE + 2)
#define BB_RETIMER_MUX_DATA_PRESENT                             \
	(USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |       \
	 USB_PD_MUX_SAFE_MODE | USB_PD_MUX_TBT_COMPAT_ENABLED | \
	 USB_PD_MUX_USB4_ENABLED)

#define BB_RETIMER_MUX_USB_MODE \
	(USB_PD_MUX_USB_ENABLED | USB_PD_MUX_USB4_ENABLED)

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

#define BB_RETIMER_I2C_RETRY 5
#define BB_RETIMER_REG_OFFSET_MAX UINT8_MAX

/*
 * Mutex for BB_RETIMER_REG_CONNECTION_STATE register, which can be
 * accessed from multiple tasks.
 */
static mutex_t bb_retimer_lock[CONFIG_USB_PD_PORT_MAX_COUNT];
/*
 * Requested BB mux state.
 */
static mux_state_t bb_mux_state[CONFIG_USB_PD_PORT_MAX_COUNT];

/**
 * Utility functions
 */
static int bb_retimer_read(const struct usb_mux *me, const uint32_t offset,
			   uint32_t *data)
{
	int rv, retry = 0;
	uint8_t buf[BB_RETIMER_READ_SIZE];

	/* Validate the register address */
	if (offset > BB_RETIMER_REG_OFFSET_MAX)
		return EC_ERROR_INVAL;

	/*
	 * This I2C message will trigger retimer's internal read sequence
	 * if its a NAK, sleep and resend same I2C
	 */
	while (1) {
		/*
		 * Read sequence
		 * Addr flags (w) - Reg offset - repeated start - Addr flags(r)
		 * byte[0]   : Read size
		 * byte[1:4] : Data [LSB -> MSB]
		 * Stop
		 */
		rv = i2c_xfer(me->i2c_port, me->i2c_addr_flags,
			      (const uint8_t *)&offset, 1, buf,
			      BB_RETIMER_READ_SIZE);

		if (rv == EC_SUCCESS)
			break;

		if (++retry >= BB_RETIMER_I2C_RETRY) {
			CPRINTS("C%d: Retimer I2C read err=%d", me->usb_port,
				rv);
			return rv;
		}
		msleep(10);
	}

	if (buf[0] != BB_RETIMER_REG_SIZE)
		return EC_ERROR_UNKNOWN;

	*data = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	return EC_SUCCESS;
}

static int bb_retimer_write(const struct usb_mux *me, const uint32_t offset,
			    uint32_t data)
{
	int rv, retry = 0;
	uint8_t buf[BB_RETIMER_WRITE_SIZE];

	/* Validate the register address */
	if (offset > BB_RETIMER_REG_OFFSET_MAX)
		return EC_ERROR_INVAL;

	/*
	 * Write sequence
	 * Addr flags(w)
	 * byte[0]   : Reg offset
	 * byte[1]   : Write Size
	 * byte[2:5] : Data [LSB -> MSB]
	 * stop
	 */
	buf[0] = (uint8_t)offset;
	buf[1] = BB_RETIMER_REG_SIZE;
	buf[2] = data & 0xFF;
	buf[3] = (data >> 8) & 0xFF;
	buf[4] = (data >> 16) & 0xFF;
	buf[5] = (data >> 24) & 0xFF;

	/*
	 * This I2C message will trigger retimer's internal write sequence
	 * if its a NAK, sleep and resend same I2C
	 */
	while (1) {
		rv = i2c_xfer(me->i2c_port, me->i2c_addr_flags, buf,
			      BB_RETIMER_WRITE_SIZE, NULL, 0);

		if (rv == EC_SUCCESS)
			break;

		if (++retry >= BB_RETIMER_I2C_RETRY) {
			CPRINTS("C%d: Retimer I2C write err=%d", me->usb_port,
				rv);
			break;
		}
		msleep(10);
	}
	return rv;
}

__overridable int bb_retimer_power_enable(const struct usb_mux *me, bool enable)
{
	const struct bb_usb_control *control = &bb_controls[me->usb_port];

	/* handle retimer's power domain */

	if (enable) {
		gpio_set_level(control->usb_ls_en_gpio, 1);
		/*
		 * Tpw, minimum time from VCC to RESET_N de-assertion is 100us.
		 * For boards that don't provide a load switch control, the
		 * retimer_init() function ensures power is up before calling
		 * this function.
		 */
		msleep(1);
		gpio_set_level(control->retimer_rst_gpio, 1);
		/*
		 * Allow 1ms time for the retimer to power up lc_domain
		 * which powers I2C controller within retimer
		 */
		msleep(1);
	} else {
		gpio_set_level(control->retimer_rst_gpio, 0);
		msleep(1);
		gpio_set_level(control->usb_ls_en_gpio, 0);
	}
	return EC_SUCCESS;
}

static void retimer_handle_usb_dfp(int port, uint32_t *set_retimer_con)
{
	enum idh_ptype cable_type = get_usb_pd_cable_type(port);
	/*
	 * Bit 2: RE_TIMER_DRIVER
	 * 0 - Re-driver
	 * 1 - Re-timer
	 *
	 * If Alternate mode is USB/USB4, RE_TIMER_DRIVER is
	 * set according to SOP' VDO2 response Bit 9.
	 *
	 */
	if (is_active_cable_element_retimer(port))
		*set_retimer_con |= BB_RETIMER_RE_TIMER_DRIVER;

	/*
	 * Bit 22: ACTIVE/PASSIVE
	 * 0 - Passive cable
	 * 1 - Active cable
	 *
	 * If the mode is USB/USB4, ACTIVE/PASIVE is
	 * set according to Discover mode SOP' response.
	 */
	if (cable_type == IDH_PTYPE_ACABLE)
		*set_retimer_con |= BB_RETIMER_ACTIVE_PASSIVE;
}

static void retimer_handle_tbt_dfp(int port, mux_state_t mux_state,
				   uint32_t *set_retimer_con)
{
	union tbt_mode_resp_cable cable_resp = {
		.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME)
	};
	union tbt_mode_resp_device dev_resp = {
		.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP)
	};
	enum idh_ptype cable_type = get_usb_pd_cable_type(port);

	/*
	 * Bit 2: RE_TIMER_DRIVER
	 * 0 - Re-driver
	 * 1 - Re-timer
	 *
	 * If Alternate mode is Thunderbolt-Compat, RE_TIMER_DRIVER is
	 * set according to Discover Mode SOP' response,
	 * Bit 22: Retimer Type.
	 */
	if (cable_resp.retimer_type == USB_RETIMER)
		*set_retimer_con |= BB_RETIMER_RE_TIMER_DRIVER;

	/*
	 * Bit 17: TBT_TYPE
	 * 0 - Type-C to Type-C Cable
	 * 1 - Type-C Legacy TBT Adapter
	 */
	if (dev_resp.tbt_adapter == TBT_ADAPTER_TBT2_LEGACY)
		*set_retimer_con |= BB_RETIMER_TBT_TYPE;

	/*
	 * Bit 18: CABLE_TYPE
	 * 0 - Electrical cable
	 * 1 - Optical cable
	 */
	if (cable_resp.tbt_cable == TBT_CABLE_OPTICAL)
		*set_retimer_con |= BB_RETIMER_TBT_CABLE_TYPE;

	/*
	 * Bit 19: VPO_DOCK_DETECTED_OR_DP_OVERDRIVE
	 * 0 - No vPro Dock.No DP Overdrive
	 *     detected
	 * 1 - vPro Dock or DP Overdrive
	 *     detected
	 */
	if ((IS_ENABLED(CONFIG_USBC_RETIMER_INTEL_BB_VPRO_CAPABLE) &&
	     dev_resp.intel_spec_b0 == VENDOR_SPECIFIC_SUPPORTED) ||
	    dev_resp.vendor_spec_b1 == VENDOR_SPECIFIC_SUPPORTED)
		*set_retimer_con |= BB_RETIMER_VPRO_DOCK_DP_OVERDRIVE;

	/*
	 * Bit 20: TBT_ACTIVE_LINK_TRAINING
	 * 0 - Active with bi-directional LSRX communication
	 * 1 - Active with uni-directional LSRX communication
	 * Set to "0" when passive cable plug
	 */
	if ((cable_type == IDH_PTYPE_ACABLE ||
	     cable_resp.tbt_active_passive == TBT_CABLE_ACTIVE) &&
	    cable_resp.lsrx_comm == UNIDIR_LSRX_COMM)
		*set_retimer_con |= BB_RETIMER_TBT_ACTIVE_LINK_TRAINING;

	/*
	 * Bit 22: ACTIVE/PASSIVE
	 * 0 - Passive cable
	 * 1 - Active cable
	 *
	 * If the mode is Thunderbolt-Compat, ACTIVE/PASIVE is
	 * set according to Discover mode SOP' response.
	 */
	if (cable_resp.tbt_active_passive == TBT_CABLE_ACTIVE)
		*set_retimer_con |= BB_RETIMER_ACTIVE_PASSIVE;

	/*
	 * Bit 27-25: USB4/TBT Cable speed
	 * 000b - No functionality
	 * 001b - USB3.1 Gen1 Cable
	 * 010b - 10Gb/s
	 * 011b - 10Gb/s and 20Gb/s
	 * 10..11b - Reserved
	 */
	*set_retimer_con |= BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(
		mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED ?
			get_tbt_cable_speed(port) :
			get_usb4_cable_speed(port));

	/*
	 * Bits 29-28: TBT_GEN_SUPPORT
	 * 00b - 3rd generation TBT (10.3125 and 20.625Gb/s)
	 * 01b - 4th generation TBT (10.00005Gb/s, 10.3125Gb/s,
	 *                           20.0625Gb/s, 20.000Gb/s)
	 * 10..11b - Reserved
	 */
	*set_retimer_con |=
		BB_RETIMER_TBT_CABLE_GENERATION(cable_resp.tbt_rounded);
}

static void retimer_handle_dp21_dfp(int port, uint32_t *set_retimer_con)
{
	union dp_mode_resp_cable cable_dp_mode_resp = {
		.raw_value =
			IS_ENABLED(CONFIG_USB_PD_DP21_MODE) ?
				dp_get_mode_vdo(port, TCPCI_MSG_SOP_PRIME) :
				0
	};
	union tbt_mode_resp_cable tbt_cable_resp = {
		.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME)
	};

	enum idh_ptype cable_type = get_usb_pd_cable_type(port);
	enum dpam_version dpam_ver =
		dp_resolve_dpam_version(port, TCPCI_MSG_SOP_PRIME);

	/*
	 * Bit 2: RE_TIMER_DRIVER
	 * 0 - Re-driver
	 * 1 - Re-timer
	 *
	 * If Alternate mode is DP2.0  or earlier RE_TIMER_DRIVER is
	 * set according to SOP' VDO2 response Bit 9.
	 *
	 * If Alternate mode is DP2.1 RE_TIMER_DRIVER is
	 * set according to DP Discover mode SOP' response Bit 29:28.
	 */
	if ((dpam_ver == DPAM_VERSION_20 &&
	     is_active_cable_element_retimer(port)) ||
	    (dpam_ver == DPAM_VERSION_21 &&
	     cable_dp_mode_resp.active_comp == DP21_ACTIVE_RETIMER_CABLE) ||
	    tbt_cable_resp.retimer_type == USB_RETIMER)
		*set_retimer_con |= BB_RETIMER_RE_TIMER_DRIVER;

	/*
	 * Bit 18: CABLE_TYPE
	 * 0 - Electrical cable
	 * 1 - Optical cable
	 */
	if ((cable_dp_mode_resp.active_comp == DP21_OPTICAL_CABLE &&
	     dpam_ver == DPAM_VERSION_21) ||
	    tbt_cable_resp.tbt_cable == TBT_CABLE_OPTICAL)
		*set_retimer_con |= BB_RETIMER_TBT_CABLE_TYPE;

	/*
	 * Bit 22: ACTIVE/PASSIVE
	 * 0 - Passive cable
	 * 1 - Active cable
	 *
	 * If the mode is DP2.1, ACTIVE/PASIVE is set according to
	 * DP Discover mode SOP' response B29:28
	 * If the mode is DP2.0 or earlier, ACTIVE/PASIVE is set according to
	 * Discover ID SOP' response B29:27.
	 */
	if (((dpam_ver == DPAM_VERSION_20) &&
	     (cable_type == IDH_PTYPE_ACABLE)) ||
	    ((dpam_ver == DPAM_VERSION_21) &&
	     (cable_dp_mode_resp.active_comp != DP21_PASSIVE_CABLE)) ||
	    tbt_cable_resp.tbt_active_passive == TBT_CABLE_ACTIVE)
		*set_retimer_con |= BB_RETIMER_ACTIVE_PASSIVE;

	/*
	 * Bit 27-25: DP Cable speed for DP2.1
	 * 000b - No functionality
	 * 001b - HBR3
	 * 010b - UHBR10
	 * 100b - UHBR20
	 */
	*set_retimer_con |= BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(
		dpam_ver == DPAM_VERSION_21 ? dp_get_cable_bit_rate(port) :
					      get_usb4_cable_speed(port));
}

static void retimer_handle_dp_dfp(int port, uint32_t *set_retimer_con)
{
	union tbt_mode_resp_cable cable_resp = {
		.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME)
	};
	enum idh_ptype cable_type = get_usb_pd_cable_type(port);

	/*
	 * Bit 2: RE_TIMER_DRIVER
	 * 0 - Re-driver
	 * 1 - Re-timer
	 *
	 * If DP2.1 feature is not enabled, RE_TIMER_DRIVER is
	 * set according to SOP' VDO2 response Bit 9.
	 */
	if (is_active_cable_element_retimer(port))
		*set_retimer_con |= BB_RETIMER_RE_TIMER_DRIVER;

	/*
	 * Bit 18: CABLE_TYPE
	 * 0 - Electrical cable
	 * 1 - Optical cable
	 */
	if (cable_resp.tbt_cable == TBT_CABLE_OPTICAL)
		*set_retimer_con |= BB_RETIMER_TBT_CABLE_TYPE;

	/*
	 * Bit 22: ACTIVE/PASSIVE
	 * 0 - Passive cable
	 * 1 - Active cable
	 *
	 * If DP2.1 support is not enabled, ACTIVE/PASIVE is set
	 * according to Discover ID SOP' response B29:27.
	 */
	if (cable_type == IDH_PTYPE_ACABLE)
		*set_retimer_con |= BB_RETIMER_ACTIVE_PASSIVE;
}

static void retimer_set_state_dfp(int port, mux_state_t mux_state,
				  uint32_t *set_retimer_con)
{
	if (mux_state & (USB_PD_MUX_USB_ENABLED | USB_PD_MUX_USB4_ENABLED))
		retimer_handle_usb_dfp(port, set_retimer_con);

	if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED ||
	    mux_state & USB_PD_MUX_USB4_ENABLED)
		retimer_handle_tbt_dfp(port, mux_state, set_retimer_con);

	if (IS_ENABLED(CONFIG_USB_PD_DP21_MODE) &&
	    (mux_state & USB_PD_MUX_DP_ENABLED)) {
		retimer_handle_dp21_dfp(port, set_retimer_con);
	} else if (!IS_ENABLED(CONFIG_USB_PD_DP21_MODE) &&
		   (mux_state & USB_PD_MUX_DP_ENABLED)) {
		retimer_handle_dp_dfp(port, set_retimer_con);
	}
}

static void retimer_set_state_ufp(int port, mux_state_t mux_state,
				  uint32_t *set_retimer_con)
{
	/*
	 * Bit 7: USB_DATA_ROLE for the Burnside Bridge side of
	 * connection.
	 * 0 - DFP
	 * 1 - UFP
	 */
	*set_retimer_con |= BB_RETIMER_USB_DATA_ROLE;

	if (!IS_ENABLED(CONFIG_USB_PD_ALT_MODE_UFP))
		return;

	/* TODO:b/168890624: Set USB4 retimer config for UFP */
	if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED) {
		union tbt_dev_mode_enter_cmd ufp_tbt_enter_mode = {
			.raw_value = pd_ufp_get_enter_mode(port)
		};
		/*
		 * Bit 2: RE_TIMER_DRIVER
		 * 0 - Re-driver
		 * 1 - Re-timer
		 *
		 * Set according to TBT3 Enter Mode bit 22.
		 */
		if (ufp_tbt_enter_mode.retimer_type == USB_RETIMER)
			*set_retimer_con |= BB_RETIMER_RE_TIMER_DRIVER;

		/*
		 * Bit 18: CABLE_TYPE
		 * 0 - Electrical cable
		 * 1 - Optical cable
		 *
		 * Set according to TBT3 Enter Mode bit 21.
		 */
		if (ufp_tbt_enter_mode.tbt_cable == TBT_CABLE_OPTICAL)
			*set_retimer_con |= BB_RETIMER_TBT_CABLE_TYPE;

		/*
		 * Bit 19: VPO_DOCK_DETECTED_OR_DP_OVERDRIVE
		 * 0 - No vPro Dock.No DP Overdrive
		 *     detected
		 * 1 - vPro Dock or DP Overdrive
		 *     detected
		 *
		 * Set according to TBT3 Enter Mode bit 26 or bit 31
		 */
		if ((IS_ENABLED(CONFIG_USBC_RETIMER_INTEL_BB_VPRO_CAPABLE) &&
		     ufp_tbt_enter_mode.intel_spec_b0 ==
			     VENDOR_SPECIFIC_SUPPORTED) ||
		    ufp_tbt_enter_mode.vendor_spec_b1 ==
			    VENDOR_SPECIFIC_SUPPORTED)
			*set_retimer_con |= BB_RETIMER_VPRO_DOCK_DP_OVERDRIVE;

		/*
		 * Bit 20: TBT_ACTIVE_LINK_TRAINING
		 * 0 - Active with bi-directional LSRX communication
		 * 1 - Active with uni-directional LSRX communication
		 *
		 * Set according to TBT3 Enter Mode bit 23
		 */
		if (ufp_tbt_enter_mode.lsrx_comm == UNIDIR_LSRX_COMM)
			*set_retimer_con |= BB_RETIMER_TBT_ACTIVE_LINK_TRAINING;

		/*
		 * Bit 22: ACTIVE/PASSIVE
		 * 0 - Passive cable
		 * 1 - Active cable
		 *
		 * Set according to TBT3 Enter Mode bit 24
		 */
		if (ufp_tbt_enter_mode.cable == TBT_ENTER_ACTIVE_CABLE)
			*set_retimer_con |= BB_RETIMER_ACTIVE_PASSIVE;

		/*
		 * Bit 27-25: TBT Cable speed
		 * 000b - No functionality
		 * 001b - USB3.1 Gen1 Cable
		 * 010b - 10Gb/s
		 * 011b - 10Gb/s and 20Gb/s
		 * 10..11b - Reserved
		 *
		 * Set according to TBT3 Enter Mode bit 18:16
		 */
		*set_retimer_con |= BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(
			ufp_tbt_enter_mode.tbt_cable_speed);
		/*
		 * Bits 29-28: TBT_GEN_SUPPORT
		 * 00b - 3rd generation TBT (10.3125 and 20.625Gb/s)
		 * 01b - 4th generation TBT (10.00005Gb/s, 10.3125Gb/s,
		 *                           20.0625Gb/s, 20.000Gb/s)
		 * 10..11b - Reserved
		 *
		 * Set according to TBT3 Enter Mode bit 20:19
		 */
		*set_retimer_con |= BB_RETIMER_TBT_CABLE_GENERATION(
			ufp_tbt_enter_mode.tbt_rounded);
	}
}

/**
 * Driver interface functions
 */
static int retimer_set_state(const struct usb_mux *me, mux_state_t mux_state,
			     bool *ack_required)
{
	uint32_t set_retimer_con = 0;
	uint8_t dp_pin_mode;
	int port = me->usb_port;
	int rv = 0;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	mutex_lock(&bb_retimer_lock[port]);
	bb_mux_state[port] = mux_state;

	/*
	 * Bit 0: DATA_CONNECTION_PRESENT
	 * 0 - No connection present
	 * 1 - Connection present
	 */
	if (mux_state & BB_RETIMER_MUX_DATA_PRESENT)
		set_retimer_con |= BB_RETIMER_DATA_CONNECTION_PRESENT;

	/*
	 * Bit 1: CONNECTION_ORIENTATION
	 * 0 - Normal
	 * 1 - reversed
	 */
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		set_retimer_con |= BB_RETIMER_CONNECTION_ORIENTATION;

	/*
	 * Bit 5: USB_3_CONNECTION
	 * 0 - No USB3.1 Connection
	 * 1 - USB3.1 connection
	 */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		set_retimer_con |= BB_RETIMER_USB_3_CONNECTION;

		/*
		 * Bit 6: USB3_Speed
		 * 0 – USB3 is limited to Gen1
		 * 1 – USB3 Gen1/Gen2 supported
		 */
		if (is_cable_speed_gen2_capable(port))
			set_retimer_con |= BB_RETIMER_USB_3_SPEED;
	}

	/*
	 * Bit 8: DP_CONNECTION
	 * 0 – No DP connection
	 * 1 – DP connected
	 *
	 * TODO: Refactor if CONFIG_USB_PD_VDM_AP_CONTROL is supported
	 */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		set_retimer_con |= BB_RETIMER_DP_CONNECTION;

		/*
		 * Bit 11-10: DP_PIN_ASSIGNMENT (ignored if BIT8 = 0)
		 * 00 – Pin assignments E/E’
		 * 01 – Pin assignments C/C’/D/D’1,2
		 * 10, 11 - reserved
		 */
		dp_pin_mode = get_dp_pin_mode(port);
		if (dp_pin_mode == MODE_DP_PIN_C ||
		    dp_pin_mode == MODE_DP_PIN_D)
			set_retimer_con |= BB_RETIMER_DP_PIN_ASSIGNMENT;

		/*
		 * Bit 14: IRQ_HPD (ignored if BIT8 = 0)
		 * 0 - No IRQ_HPD
		 * 1 - IRQ_HPD received
		 */
		if (mux_state & USB_PD_MUX_HPD_IRQ)
			set_retimer_con |= BB_RETIMER_IRQ_HPD;

		/*
		 * Bit 15: HPD_LVL (ignored if BIT8 = 0)
		 * 0 - HPD_State Low
		 * 1 - HPD_State High
		 */
		if (mux_state & USB_PD_MUX_HPD_LVL)
			set_retimer_con |= BB_RETIMER_HPD_LVL;
	}

	/*
	 * Bit 16: TBT_CONNECTION
	 * 0 - TBT not configured
	 * 1 - TBT configured
	 */
	if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED)
		set_retimer_con |= BB_RETIMER_TBT_CONNECTION;

	/*
	 * Bit 23: USB4_CONNECTION
	 * 0 - USB4 not configured
	 * 1 - USB4 Configured
	 */
	if (mux_state & USB_PD_MUX_USB4_ENABLED)
		set_retimer_con |= BB_RETIMER_USB4_ENABLED;

	if (pd_get_data_role(port) == PD_ROLE_DFP)
		retimer_set_state_dfp(port, mux_state, &set_retimer_con);
	else
		retimer_set_state_ufp(port, mux_state, &set_retimer_con);

	/* Writing the register4 */
	rv = bb_retimer_write(me, BB_RETIMER_REG_CONNECTION_STATE,
			      set_retimer_con);
	mutex_unlock(&bb_retimer_lock[port]);
	return rv;
}

static int bb_set_idle_mode(const struct usb_mux *me, bool idle)
{
	bool usb3_enable;
	int rv;
	uint32_t reg_val;
	int port = me->usb_port;

	mutex_lock(&bb_retimer_lock[port]);

	if (!(bb_mux_state[port] & USB_PD_MUX_USB_ENABLED)) {
		mutex_unlock(&bb_retimer_lock[port]);
		return EC_SUCCESS;
	}

	rv = bb_retimer_read(me, BB_RETIMER_REG_CONNECTION_STATE, &reg_val);
	if (rv != EC_SUCCESS) {
		mutex_unlock(&bb_retimer_lock[port]);
		return rv;
	}

	usb3_enable = !idle;

	/* Bit 5: BB_RETIMER_USB_3_CONNECTION */
	WRITE_BIT(reg_val, 5, usb3_enable);
	rv = bb_retimer_write(me, BB_RETIMER_REG_CONNECTION_STATE, reg_val);

	mutex_unlock(&bb_retimer_lock[port]);

	return rv;
}

int bb_retimer_set_dp_connection(const struct usb_mux *me, bool enable)
{
	int rv;
	uint32_t reg_val;
	int port = me->usb_port;

	mutex_lock(&bb_retimer_lock[port]);

	rv = bb_retimer_read(me, BB_RETIMER_REG_CONNECTION_STATE, &reg_val);
	if (rv != EC_SUCCESS) {
		mutex_unlock(&bb_retimer_lock[port]);
		return rv;
	}
	/* Bit 8: BB_RETIMER_DP_CONNECTION */
	WRITE_BIT(reg_val, 8, enable);
	rv = bb_retimer_write(me, BB_RETIMER_REG_CONNECTION_STATE, reg_val);

	mutex_unlock(&bb_retimer_lock[port]);

	return rv;
}

void bb_retimer_hpd_update(const struct usb_mux *me, mux_state_t hpd_state,
			   bool *ack_required)
{
	uint32_t retimer_con_reg = 0;
	int port = me->usb_port;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	mutex_lock(&bb_retimer_lock[port]);
	bb_mux_state[port] = (bb_mux_state[port] & ~MUX_STATE_HPD_UPDATE_MASK) |
			     (hpd_state & MUX_STATE_HPD_UPDATE_MASK);

	if (bb_retimer_read(me, BB_RETIMER_REG_CONNECTION_STATE,
			    &retimer_con_reg) != EC_SUCCESS) {
		mutex_unlock(&bb_retimer_lock[port]);
		return;
	}
	/*
	 * Bit 14: IRQ_HPD (ignored if BIT8 = 0)
	 * 0 - No IRQ_HPD
	 * 1 - IRQ_HPD received
	 */
	if (hpd_state & USB_PD_MUX_HPD_IRQ)
		retimer_con_reg |= BB_RETIMER_IRQ_HPD;
	else
		retimer_con_reg &= ~BB_RETIMER_IRQ_HPD;

	/*
	 * Bit 15: HPD_LVL (ignored if BIT8 = 0)
	 * 0 - HPD_State Low
	 * 1 - HPD_State High
	 */

	if (hpd_state & USB_PD_MUX_HPD_LVL)
		retimer_con_reg |= BB_RETIMER_HPD_LVL;
	else
		retimer_con_reg &= ~BB_RETIMER_HPD_LVL;

	/* Writing the register4 */
	bb_retimer_write(me, BB_RETIMER_REG_CONNECTION_STATE, retimer_con_reg);

	mutex_unlock(&bb_retimer_lock[port]);
}

#ifdef CONFIG_ZEPHYR
static void init_retimer_mutexes(void)
{
	int port;

	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		k_mutex_init(&bb_retimer_lock[port]);
	}
}
DECLARE_HOOK(HOOK_INIT, init_retimer_mutexes, HOOK_PRIO_FIRST);
#endif

static int retimer_low_power_mode(const struct usb_mux *me)
{
	const int port = me->usb_port;

	bb_mux_state[port] = USB_PD_MUX_NONE;
	return bb_retimer_power_enable(me, false);
}

static bool is_retimer_fw_update_capable(void)
{
	return true;
}

static int retimer_init(const struct usb_mux *me)
{
	int rv;
	uint32_t data;
	const int port = me->usb_port;

	bb_mux_state[port] = USB_PD_MUX_NONE;

	/* Burnside Bridge is powered by main AP rail */
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF)) {
		/* Ensure reset is asserted while chip is not powered */
		bb_retimer_power_enable(me, false);
		return EC_ERROR_NOT_POWERED;
	}

	rv = bb_retimer_power_enable(me, true);
	if (rv != EC_SUCCESS)
		return rv;

	rv = bb_retimer_read(me, BB_RETIMER_REG_VENDOR_ID, &data);
	/*
	 * After reset, i2c controller may not be ready, if this fails,
	 * retry one more time.
	 * TODO: revisit the delay time after retimer reset.
	 */
	if (rv != EC_SUCCESS)
		rv = bb_retimer_read(me, BB_RETIMER_REG_VENDOR_ID, &data);
	if (rv != EC_SUCCESS)
		return rv;
	CPRINTS("C%d: retimer power enable success", me->usb_port);
#ifdef CONFIG_USBC_RETIMER_INTEL_HB
	if (data != BB_RETIMER_DEVICE_ID)
		return EC_ERROR_INVAL;
#else
	if ((data != BB_RETIMER_VENDOR_ID_1) && data != BB_RETIMER_VENDOR_ID_2)
		return EC_ERROR_INVAL;

	rv = bb_retimer_read(me, BB_RETIMER_REG_DEVICE_ID, &data);
	if (rv != EC_SUCCESS)
		return rv;
	if (data != BB_RETIMER_DEVICE_ID)
		return EC_ERROR_INVAL;
#endif

	return EC_SUCCESS;
}

const struct usb_mux_driver bb_usb_retimer = {
	.init = retimer_init,
	.set = retimer_set_state,
	.set_idle_mode = bb_set_idle_mode,
	.enter_low_power_mode = retimer_low_power_mode,
	.is_retimer_fw_update_capable = is_retimer_fw_update_capable,
#ifdef CONFIG_CMD_RETIMER
	.retimer_read = bb_retimer_read,
	.retimer_write = bb_retimer_write,
#endif /* CONFIG_CMD_RETIMER */
};
