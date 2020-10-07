/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#include "bb_retimer.h"
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

/* Mutex for shared NVM access */
static struct mutex bb_nvm_mutex;

/**
 * Utility functions
 */
static int bb_retimer_read(const struct usb_mux *me,
			   const uint8_t offset, uint32_t *data)
{
	int rv;
	uint8_t buf[BB_RETIMER_READ_SIZE];

	/*
	 * Read sequence
	 * Slave Addr(w) - Reg offset - repeated start - Slave Addr(r)
	 * byte[0]   : Read size
	 * byte[1:4] : Data [LSB -> MSB]
	 * Stop
	 */
	rv = i2c_xfer(me->i2c_port, me->i2c_addr_flags,
		      &offset, 1, buf, BB_RETIMER_READ_SIZE);
	if (rv)
		return rv;
	if (buf[0] != BB_RETIMER_REG_SIZE)
		return EC_ERROR_UNKNOWN;

	*data = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	return EC_SUCCESS;
}

static int bb_retimer_write(const struct usb_mux *me,
			    const uint8_t offset, uint32_t data)
{
	uint8_t buf[BB_RETIMER_WRITE_SIZE];

	/*
	 * Write sequence
	 * Slave Addr(w)
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

	return i2c_xfer(me->i2c_port,
			me->i2c_addr_flags,
			buf, BB_RETIMER_WRITE_SIZE, NULL, 0);
}

__overridable void bb_retimer_power_handle(const struct usb_mux *me, int on_off)
{
	const struct bb_usb_control *control = &bb_controls[me->usb_port];

	/* handle retimer's power domain */

	if (on_off) {
		/*
		 * BB retimer NVM can be shared between multiple ports, hence
		 * lock enabling the retimer until the current retimer request
		 * is complete.
		 */
		mutex_lock(&bb_nvm_mutex);

		gpio_set_level(control->usb_ls_en_gpio, 1);
		/*
		 * Tpw, minimum time from VCC to RESET_N de-assertion is 100us.
		 * For boards that don't provide a load switch control, the
		 * retimer_init() function ensures power is up before calling
		 * this function.
		 */
		msleep(1);
		gpio_set_level(control->retimer_rst_gpio, 1);

		/* Allow 20ms time for the retimer to be initialized. */
		msleep(20);

		mutex_unlock(&bb_nvm_mutex);
	} else {
		gpio_set_level(control->retimer_rst_gpio, 0);
		msleep(1);
		gpio_set_level(control->usb_ls_en_gpio, 0);
	}
}

static void retimer_set_state_dfp(int port, mux_state_t mux_state,
				  uint32_t *set_retimer_con)
{
	union tbt_mode_resp_cable cable_resp;
	union tbt_mode_resp_device dev_resp;
	enum idh_ptype cable_type = get_usb_pd_cable_type(port);

	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/*
		 * Bit 4: USB2_CONNECTION (ignored if BIT5=0).
		 * 0 - No USB2 Connection
		 * 1 - USB2 connection
		 *
		 * For passive cable, USB2_CONNECTION = 1
		 * For active cable, USB2_CONNECTION =
		 * According to Active cable VDO2 Bit 5, USB 2.0 support.
		 */
		if (is_usb2_cable_support(port))
			*set_retimer_con |= BB_RETIMER_USB_2_CONNECTION;
	}

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
	    (cable_type == IDH_PTYPE_ACABLE))
		*set_retimer_con |= BB_RETIMER_ACTIVE_PASSIVE;

	if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED) {
		cable_resp.raw_value =
			pd_get_tbt_mode_vdo(port, TCPC_TX_SOP_PRIME);
		dev_resp.raw_value = pd_get_tbt_mode_vdo(port, TCPC_TX_SOP);

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
		if (cable_type == IDH_PTYPE_ACABLE &&
		    cable_resp.lsrx_comm == UNIDIR_LSRX_COMM)
			*set_retimer_con |= BB_RETIMER_TBT_ACTIVE_LINK_TRAINING;

		/*
		 * Bit 27-25: TBT Cable speed
		 * 000b - No functionality
		 * 001b - USB3.1 Gen1 Cable
		 * 010b - 10Gb/s
		 * 011b - 10Gb/s and 20Gb/s
		 * 10..11b - Reserved
		 */
		*set_retimer_con |= BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(
						get_tbt_cable_speed(port));
		/*
		 * Bits 29-28: TBT_GEN_SUPPORT
		 * 00b - 3rd generation TBT (10.3125 and 20.625Gb/s)
		 * 01b - 4th generation TBT (10.00005Gb/s, 10.3125Gb/s,
		 *                           20.0625Gb/s, 20.000Gb/s)
		 * 10..11b - Reserved
		 */
		*set_retimer_con |= BB_RETIMER_TBT_CABLE_GENERATION(
				       cable_resp.tbt_rounded);
	} else if (mux_state & USB_PD_MUX_USB4_ENABLED) {
		/*
		 * Bit 27-25: USB4 Cable speed
		 * 000b - No functionality
		 * 001b - USB3.1 Gen1 Cable
		 * 010b - 10Gb/s
		 * 011b - 10Gb/s and 20Gb/s
		 * 10..11b - Reserved
		 */
		*set_retimer_con |= BB_RETIMER_USB4_TBT_CABLE_SPEED_SUPPORT(
					get_usb4_cable_speed(port));
	}
}

static void retimer_set_state_ufp(mux_state_t mux_state,
				  uint32_t *set_retimer_con)
{
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		/*
		 * Bit 4: USB2_CONNECTION (ignored if BIT5=0).
		 * 0 - No USB2 Connection
		 * 1 - USB2 connection
		 *
		 * Don't care
		 */

		/*
		 * Bit 7: USB_DATA_ROLE for the Burnside Bridge side of
		 * connection (ignored if BIT5=0).
		 * 0 - DFP
		 * 1 - UFP
		 */
		*set_retimer_con |= BB_RETIMER_USB_DATA_ROLE;
	}

	 /*
	  * Bit 17: TBT_TYPE
	  * 0 - Type-C to Type-C Cable
	  * 1 - Type-C Legacy TBT Adapter
	  * For UFP, TBT_TYPE = 0
	  */

	/*
	 * TODO: b/157163664: Add the following bits:
	 *
	 * Bit 2: RE_TIMER_DRIVER:
	 * Set according to b20:19 of enter USB.
	 *
	 * Bit 18: CABLE_TYPE:
	 * For Thunderbolt-compat mode, set according to bit 21 of enter mode.
	 * For USB/DP/USB4, set according to bits 20:19 of enter mode.
	 *
	 * Bit 20: TBT_ACTIVE_LINK_TRAINING:
	 * For Thunderbolt-compat mode, set according to bit 23 of enter mode.
	 * For USB, set to 0.
	 *
	 * Bit 22: ACTIVE/PASSIVE
	 * For USB4, set according to bits 20:19 of enter USB SOP.
	 * For thubderbolt-compat mode, set according to bit 24 of enter mode.
	 *
	 * Bits 29-28: TBT_GEN_SUPPORT
	 * For Thunderbolt-compat mode, set according to bits 20:19 of enter
	 * mode.
	 */
}

/**
 * Driver interface functions
 */
static int retimer_set_state(const struct usb_mux *me, mux_state_t mux_state)
{
	uint32_t set_retimer_con = 0;
	uint8_t dp_pin_mode;
	int port = me->usb_port;
	/*
	 * TODO(b/161327513): Remove this once we have final fix for
	 * the Type-C MFD degradation issue.
	 * In alternate mode, mux changes states as USB->Safe->DP Alt Mode.
	 * As EC programs retimer into safe mode independent of virtual mux,
	 * the super speed lanes are terminated while IOM is in the process
	 * of establishing the super speed link, which causes a fallback to
	 * USB 2.0 enumeration through PCH. By removing the Safe mode in retimer
	 * Super Speed lanes are available to virtual mux and would not
	 * interrupt the enumeration process and then entering safe.
	 * From the protocol analyser traces the safe mode is still achieved
	 * with virtual mux Safe mode settings.
	 */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		return 0;
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
		retimer_set_state_ufp(mux_state, &set_retimer_con);

	/* Writing the register4 */
	return bb_retimer_write(me, BB_RETIMER_REG_CONNECTION_STATE,
			set_retimer_con);
}

static int retimer_low_power_mode(const struct usb_mux *me)
{
	bb_retimer_power_handle(me, 0);
	return EC_SUCCESS;
}

static int retimer_init(const struct usb_mux *me)
{
	int rv;
	uint32_t data;

	/* Burnside Bridge is powered by main AP rail */
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF)) {
		/* Ensure reset is asserted while chip is not powered */
		bb_retimer_power_handle(me, 0);
		return EC_ERROR_NOT_POWERED;
	}

	bb_retimer_power_handle(me, 1);

	rv = bb_retimer_read(me, BB_RETIMER_REG_VENDOR_ID, &data);
	if (rv)
		return rv;
	if (data != BB_RETIMER_VENDOR_ID)
		return EC_ERROR_UNKNOWN;

	rv = bb_retimer_read(me, BB_RETIMER_REG_DEVICE_ID, &data);
	if (rv)
		return rv;

	if (data != BB_RETIMER_DEVICE_ID)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

const struct usb_mux_driver bb_usb_retimer = {
	.init = retimer_init,
	.set = retimer_set_state,
	.enter_low_power_mode = retimer_low_power_mode,
};

#ifdef CONFIG_CMD_RETIMER
static int console_command_bb_retimer(int argc, char **argv)
{
	char rw, *e;
	int rv, port, reg, data, val;
	const struct usb_mux *mux;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	/* Get port number */
	port = strtoi(argv[1], &e, 0);
	if (*e || port < 0 || port > board_get_usb_pd_port_count())
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

	if (rw == 'r')
		rv = bb_retimer_read(mux, reg, &data);
	else {
		/* Get value to be written */
		val = strtoi(argv[4], &e, 0);
		if (*e || val < 0)
			return EC_ERROR_PARAM4;

		rv = bb_retimer_write(mux, reg, val);
		if (rv == EC_SUCCESS) {
			rv = bb_retimer_read(mux, reg, &data);
			if (rv == EC_SUCCESS && data != val)
				rv = EC_ERROR_UNKNOWN;
		}
	}

	if (rv == EC_SUCCESS)
		CPRINTS("register 0x%x [%d] = 0x%x [%d]", reg, reg, data, data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(bb, console_command_bb_retimer,
			"<port> <r/w> <reg> | <val>",
			"Read or write to BB retimer register");
#endif /* CONFIG_CMD_RETIMER */
