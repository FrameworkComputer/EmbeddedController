/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#include "driver/retimer/bb_retimer.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

#define BB_RETIMER_REG_SIZE	4
#define BB_RETIMER_READ_SIZE	(BB_RETIMER_REG_SIZE + 1)
#define BB_RETIMER_WRITE_SIZE	(BB_RETIMER_REG_SIZE + 2)
#define BB_RETIMER_MUX_DATA_PRESENT (USB_PD_MUX_USB_ENABLED \
				| USB_PD_MUX_DP_ENABLED \
				| USB_PD_MUX_SAFE_MODE \
				| USB_PD_MUX_TBT_COMPAT_ENABLED \
				| USB_PD_MUX_USB4_ENABLED)

#define BB_RETIMER_MUX_USB_ALT_MODE (USB_PD_MUX_USB_ENABLED\
				| USB_PD_MUX_DP_ENABLED \
				| USB_PD_MUX_TBT_COMPAT_ENABLED \
				| USB_PD_MUX_USB4_ENABLED)

#define BB_RETIMER_MUX_USB_DP_MODE (USB_PD_MUX_USB_ENABLED \
				| USB_PD_MUX_DP_ENABLED \
				| USB_PD_MUX_USB4_ENABLED)

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define BB_RETIMER_I2C_RETRY	5

/**
 * Utility functions
 */
static int bb_retimer_read(const struct usb_mux *me,
			   const uint8_t offset, uint32_t *data)
{
	int rv, retry = 0;
	uint8_t buf[BB_RETIMER_READ_SIZE];

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
		      &offset, 1, buf, BB_RETIMER_READ_SIZE);

		if (rv == EC_SUCCESS)
			break;

		if (++retry >= BB_RETIMER_I2C_RETRY) {
			CPRINTS("C%d: Retimer I2C read err=%d",
				me->usb_port, rv);
			return rv;
		}
		msleep(10);
	}

	if (buf[0] != BB_RETIMER_REG_SIZE)
		return EC_ERROR_UNKNOWN;

	*data = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	return EC_SUCCESS;
}

static int bb_retimer_write(const struct usb_mux *me,
			    const uint8_t offset, uint32_t data)
{
	int rv, retry = 0;
	uint8_t buf[BB_RETIMER_WRITE_SIZE];

	/*
	 * Write sequence
	 * Addr flags(w)
	 * byte[0]   : Reg offset
	 * byte[1]   : Write Size
	 * byte[2:5] : Data [LSB -> MSB]
	 * stop
	 */
	buf[0] = offset;
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
			CPRINTS("C%d: Retimer I2C write err=%d",
				me->usb_port, rv);
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

static void retimer_set_state_dfp(int port, mux_state_t mux_state,
				  uint32_t *set_retimer_con)
{
	union tbt_mode_resp_cable cable_resp = {
		.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME) };
	union tbt_mode_resp_device dev_resp;
	enum idh_ptype cable_type = get_usb_pd_cable_type(port);

	/*
	 * Bit 2: RE_TIMER_DRIVER
	 * 0 - Re-driver
	 * 1 - Re-timer
	 *
	 * If Alternate mode is USB/DP/USB4, RE_TIMER_DRIVER is
	 * set according to SOP' VDO2 response Bit 9.
	 *
	 */
	if (is_active_cable_element_retimer(port) &&
	   (mux_state & BB_RETIMER_MUX_USB_DP_MODE))
		*set_retimer_con |= BB_RETIMER_RE_TIMER_DRIVER;

	/*
	 * Bit 22: ACTIVE/PASSIVE
	 * 0 - Passive cable
	 * 1 - Active cable
	 *
	 * If the mode is USB/DP/Thunderbolt_compat/USB4, ACTIVE/PASIVE is
	 * set according to Discover mode SOP' response.
	 */
	if ((mux_state & BB_RETIMER_MUX_USB_ALT_MODE) &&
	    ((cable_type == IDH_PTYPE_ACABLE) ||
	      cable_resp.tbt_active_passive == TBT_CABLE_ACTIVE))
		*set_retimer_con |= BB_RETIMER_ACTIVE_PASSIVE;

	if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED ||
	    mux_state & USB_PD_MUX_USB4_ENABLED) {
		dev_resp.raw_value = pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP);

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
		if (dev_resp.intel_spec_b0 == VENDOR_SPECIFIC_SUPPORTED ||
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
		*set_retimer_con |= BB_RETIMER_TBT_CABLE_GENERATION(
				       cable_resp.tbt_rounded);
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
			.raw_value = pd_ufp_get_enter_mode(port)};
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
		if (ufp_tbt_enter_mode.intel_spec_b0 ==
					VENDOR_SPECIFIC_SUPPORTED ||
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
 * Driver interface function: reset retimer
 */
__overridable int bb_retimer_reset(const struct usb_mux *me)
{
	/*
	 * TODO(b/193402306, b/195375738): Remove this once transition to
	 * QS Silicon is complete
	 */
	return EC_SUCCESS;
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

	/* This driver does not use host command ACKs */
	*ack_required = false;

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

	/*
	 * In AP Mode DP exit to TBT entry is causing TBT lane bonding issue
	 * Issue is not seen by calling the retimer reset as WA at the time of
	 * disconnect mode configuration
	 */
	if (mux_state == USB_PD_MUX_NONE)
		bb_retimer_reset(me);

	/* Writing the register4 */
	return bb_retimer_write(me, BB_RETIMER_REG_CONNECTION_STATE,
			set_retimer_con);
}

static int retimer_low_power_mode(const struct usb_mux *me)
{
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
	if (rv != EC_SUCCESS)
		return rv;
	if ((data != BB_RETIMER_VENDOR_ID_1) &&
			data != BB_RETIMER_VENDOR_ID_2)
		return EC_ERROR_INVAL;

	rv = bb_retimer_read(me, BB_RETIMER_REG_DEVICE_ID, &data);
	if (rv != EC_SUCCESS)
		return rv;
	if (data != BB_RETIMER_DEVICE_ID)
		return EC_ERROR_INVAL;

	return EC_SUCCESS;
}

const struct usb_mux_driver bb_usb_retimer = {
	.init = retimer_init,
	.set = retimer_set_state,
	.enter_low_power_mode = retimer_low_power_mode,
	.is_retimer_fw_update_capable = is_retimer_fw_update_capable,
};

#ifdef CONFIG_CMD_RETIMER
static int console_command_bb_retimer(int argc, char **argv)
{
	char rw, *e;
	int port, reg, data, val = 0;
	int rv = EC_SUCCESS;
	const struct usb_mux *mux;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	/* Get port number */
	port = strtoi(argv[1], &e, 0);
	if (*e || !board_is_usb_pd_port_present(port))
		return EC_ERROR_PARAM1;

	mux = &usb_muxes[port];
	while (mux) {
		if (mux->driver == &bb_usb_retimer)
			break;
		mux = mux->next_mux;
	}

	if (!mux)
		return EC_ERROR_PARAM1;

	/* Validate r/w selection */
	rw = argv[2][0];
	if (rw != 'w' && rw != 'r')
		return EC_ERROR_PARAM2;

	/* Get register address */
	reg = strtoi(argv[3], &e, 0);
	if (*e || reg < 0)
		return EC_ERROR_PARAM3;

	/* Get value to be written */
	if (rw == 'w') {
		val = strtoi(argv[4], &e, 0);
		if (*e || val < 0)
			return EC_ERROR_PARAM4;
	}

	for (; mux != NULL; mux = mux->next_mux) {
		if (mux->driver == &bb_usb_retimer) {
			if (rw == 'r')
				rv = bb_retimer_read(mux, reg, &data);
			else {
				rv = bb_retimer_write(mux, reg, val);
				if (rv == EC_SUCCESS) {
					rv = bb_retimer_read(
						mux, reg, &data);
				if (rv == EC_SUCCESS && data != val)
					rv = EC_ERROR_UNKNOWN;
				}
			}
			if (rv == EC_SUCCESS)
				CPRINTS("Addr 0x%x register %d = 0x%x",
					mux->i2c_addr_flags, reg, data);
		}
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(bb, console_command_bb_retimer,
			"<port> <r/w> <reg> | <val>",
			"Read or write to BB retimer register");
#endif /* CONFIG_CMD_RETIMER */
