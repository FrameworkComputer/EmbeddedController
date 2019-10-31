/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Driver for Intel Burnside Bridge - Thunderbolt/USB/DisplayPort Retimer
 */

#include "bb_retimer.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "timer.h"
#include "usb_pd.h"
#include "usb_retimer.h"
#include "util.h"

#define BB_RETIMER_REG_SIZE	4
#define BB_RETIMER_READ_SIZE	(BB_RETIMER_REG_SIZE + 1)
#define BB_RETIMER_WRITE_SIZE	(BB_RETIMER_REG_SIZE + 2)

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static int bb_retimer_read(int port, const uint8_t offset, uint32_t *data)
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
	rv = i2c_xfer(bb_retimers[port].i2c_port, bb_retimers[port].i2c_addr,
			&offset, 1, buf, BB_RETIMER_READ_SIZE);
	if (rv)
		return rv;
	if (buf[0] != BB_RETIMER_REG_SIZE)
		return EC_ERROR_UNKNOWN;

	*data = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	return EC_SUCCESS;
}

static int bb_retimer_write(int port, const uint8_t offset, uint32_t data)
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

	return i2c_xfer(bb_retimers[port].i2c_port, bb_retimers[port].i2c_addr,
			buf, BB_RETIMER_WRITE_SIZE, NULL, 0);
}

static void bb_retimer_power_handle(int port, int on_off)
{
	struct bb_retimer *retimer;

	/* handle retimer's power domain */
	retimer = &bb_retimers[port];

	if (on_off) {
		gpio_set_level(retimer->usb_ls_en_gpio, 1);
		msleep(1);
		gpio_set_level(retimer->retimer_rst_gpio, 1);
		msleep(10);
		gpio_set_level(retimer->force_power_gpio, 1);

		/*
		 * If BB retimer NVM is shared between two ports allow 40ms
		 * time for both retimers to be initialized. Else allow 20ms
		 * to initialize.
		 */
		if (retimer->shared_nvm)
			msleep(40);
		else
			msleep(20);
	} else {
		gpio_set_level(retimer->force_power_gpio, 0);
		msleep(1);
		gpio_set_level(retimer->retimer_rst_gpio, 0);
		msleep(1);
		gpio_set_level(retimer->usb_ls_en_gpio, 0);
	}
}

int retimer_set_state(int port, mux_state_t mux_state)
{
	uint32_t set_retimer_con = 0;
	uint8_t dp_pin_mode;

	/*
	 * Bit 0: DATA_CONNECTION_PRESENT
	 * 0 - No connection present
	 * 1 - Connection present
	 */
	if (mux_state & USB_PD_MUX_USB_ENABLED ||
		mux_state & USB_PD_MUX_DP_ENABLED)
		set_retimer_con |= BB_RETIMER_DATA_CONNECTION_PRESENT;

	/*
	 * Bit 1: CONNECTION_ORIENTATION
	 * 0 - Normal
	 * 1 - reversed
	 */
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		set_retimer_con |= BB_RETIMER_CONNECTION_ORIENTATION;

	/*
	 * TODO: b:129990370
	 * Bit 2: ACTIVE_CABLE
	 * 0 - Passive
	 * 1 -TBT Active cable
	 */

	/*
	 * Bit 5: USB_3_CONNECTION
	 * 0 - No USB3.1 Connection
	 * 1 - USB3.1 connection
	 */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		set_retimer_con |= BB_RETIMER_USB_3_CONNECTION;

		/*
		 * Bit 7: USB_DATA_ROLE (ignored if BIT5=0)
		 * 0 - DFP
		 * 1 - UPF
		 */
		if (pd_partner_is_ufp(port))
			set_retimer_con |= BB_RETIMER_USB_DATA_ROLE;
	}

	/*
	 * Bit 8: DP_CONNECTION
	 * 0 – No DP connection
	 * 1 – DP connected
	 */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		set_retimer_con |= BB_RETIMER_DP_CONNECTION;

		/*
		 * Bit 10-11: DP_PIN_ASSIGNMENT (ignored if BIT8 = 0)
		 * 00 – Pin assignments E/E’
		 * 01 – Pin assignments C/C’/D/D’1,2
		 * 10, 11 - reserved
		 */
		dp_pin_mode = board_get_dp_pin_mode(port);
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
	 * Bit 12: DEBUG_ACCESSORY_MODE
	 * 0 - Not in debug mode
	 * 1 - In debug accessory mode
	 */
	if (pd_is_debug_acc(port))
		set_retimer_con |= BB_RETIMER_DEBUG_ACCESSORY_MODE;

	/* Writing the register4 */
	return bb_retimer_write(port, BB_RETIMER_REG_CONNECTION_STATE,
			set_retimer_con);
}

int retimer_low_power_mode(int port)
{
	bb_retimer_power_handle(port, 0);
	return EC_SUCCESS;
}

int retimer_init(int port)
{
	int rv;
	uint32_t data;

	bb_retimer_power_handle(port, 1);

	rv = bb_retimer_read(port, BB_RETIMER_REG_VENDOR_ID, &data);
	if (rv)
		return rv;
	if (data != BB_RETIMER_VENDOR_ID)
		return EC_ERROR_UNKNOWN;

	rv = bb_retimer_read(port, BB_RETIMER_REG_DEVICE_ID, &data);
	if (rv)
		return rv;

	if (data != BB_RETIMER_DEVICE_ID)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_RETIMER
static int console_command_bb_retimer(int argc, char **argv)
{
	char rw, *e;
	int rv, port, reg, data, val;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	/* Get port number */
	port = strtoi(argv[1], &e, 0);
	if (*e || port < 0 || port > CONFIG_USB_PD_PORT_COUNT)
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
		rv = bb_retimer_read(port, reg, &data);
	else {
		/* Get value to be written */
		val = strtoi(argv[4], &e, 0);
		if (*e || val < 0)
			return EC_ERROR_PARAM4;

		rv = bb_retimer_write(port, reg, val);
		if (rv == EC_SUCCESS) {
			rv = bb_retimer_read(port, reg, &data);
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
