/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMD FP6 USB/DP Mux.
 */

#include "amd_fp6.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "queue.h"
#include "timer.h"
#include "usb_mux.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTFUSB(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/*
 * The recommendation from "3.3.2 Command Timeout" is 250ms,
 * however empirical testing found that a 100ms timeout is sufficient.
 */
#define WRITE_CMD_TIMEOUT_MS 100

/* Command retry interval */
#define CMD_RETRY_INTERVAL_MS 1000

/*
 * Local data structure for saving mux state so it can be restored after
 * an AP reset.
 */
static struct {
	const struct usb_mux *mux;
	uint8_t val;
	bool write_pending;
} saved_mux_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static int amd_fp6_mux_port0_read(const struct usb_mux *me, uint8_t *val)
{
	uint8_t payload[3] = { 0 };
	bool mux_ready;

	RETURN_ERROR(i2c_xfer(me->i2c_port, me->i2c_addr_flags, NULL, 0,
			      payload, 3));

	/*
	 * payload[0]: Status/ID
	 * payload[1]: Port 0 Control/Status
	 * payload[2]: Port 1 Control/Status (unused on FP6)
	 */
	mux_ready = !!((payload[0] >> AMD_FP6_MUX_PD_STATUS_OFFSET) &
		       AMD_FP6_MUX_PD_STATUS_READY);

	if (!mux_ready)
		return EC_ERROR_BUSY;
	*val = payload[1];

	return EC_SUCCESS;
}

static int amd_fp6_mux_port0_write(const struct usb_mux *me, uint8_t write_val)
{
	uint8_t read_val;
	uint8_t port_status;
	timestamp_t start;

	/* Check if mux is ready */
	RETURN_ERROR(amd_fp6_mux_port0_read(me, &read_val));

	/* Write control register */
	RETURN_ERROR(
		i2c_write8(me->i2c_port, me->i2c_addr_flags, 0, write_val));

	/*
	 * Read status until write command finishes or times out.
	 * The mux has an internal opaque timeout, which we wrap with our own
	 * timeout to be safe.
	 */
	start = get_time();
	while (time_since32(start) < WRITE_CMD_TIMEOUT_MS * MSEC) {
		RETURN_ERROR(amd_fp6_mux_port0_read(me, &read_val));
		port_status = read_val >> AMD_FP6_MUX_PORT_STATUS_OFFSET;

		if (port_status == AMD_FP6_MUX_PORT_CMD_COMPLETE)
			return EC_SUCCESS;
		else if (port_status == AMD_FP6_MUX_PORT_CMD_TIMEOUT)
			return EC_ERROR_TIMEOUT;
		else if (port_status == AMD_FP6_MUX_PORT_CMD_BUSY)
			crec_msleep(5);
		else
			return EC_ERROR_UNKNOWN;
	}

	return EC_ERROR_TIMEOUT;
}

/*
 * Keep trying to write the saved mux state until successful or SOC leaves
 * S0 power state.
 */
static void amd_fp6_set_mux_retry(void);
DECLARE_DEFERRED(amd_fp6_set_mux_retry);
static void amd_fp6_set_mux_retry(void)
{
	int rv;
	bool try_again = false;

	/*
	 * Mux can only be written in S0, stop trying.
	 * Will try again on chipset_resume.
	 */
	if (!chipset_in_state(CHIPSET_STATE_ON))
		return;

	for (int i = 0; i < ARRAY_SIZE(saved_mux_state); i++) {
		/* Make sure mux_state is initialized */
		if (saved_mux_state[i].mux == NULL ||
		    !saved_mux_state[i].write_pending)
			continue;

		rv = amd_fp6_mux_port0_write(saved_mux_state[i].mux,
					     saved_mux_state[i].val);

		if (rv)
			try_again = true;
		else
			saved_mux_state[i].write_pending = false;
	}
	if (try_again)
		hook_call_deferred(&amd_fp6_set_mux_retry_data,
				   CMD_RETRY_INTERVAL_MS * MSEC);
}

static int amd_fp6_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			   bool *ack_required)
{
	uint8_t val;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* This driver treats safe mode as none */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	if (mux_state == USB_PD_MUX_NONE)
		/*
		 * LOW_POWER must be set when connection mode is
		 * set to 00b (safe state)
		 */
		val = AMD_FP6_MUX_MODE_SAFE | AMD_FP6_MUX_LOW_POWER;
	else if ((mux_state & USB_PD_MUX_USB_ENABLED) &&
		 (mux_state & USB_PD_MUX_DP_ENABLED))
		val = AMD_FP6_MUX_MODE_DOCK;
	else if (mux_state & USB_PD_MUX_USB_ENABLED)
		val = AMD_FP6_MUX_MODE_USB;
	else if (mux_state & USB_PD_MUX_DP_ENABLED)
		val = AMD_FP6_MUX_MODE_DP;
	else {
		CPRINTSUSB("C%d: unhandled mux_state %x\n", me->usb_port,
			   mux_state);
		return EC_ERROR_INVAL;
	}

	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		val |= AMD_FP6_MUX_ORIENTATION;

	saved_mux_state[me->usb_port].mux = me;
	saved_mux_state[me->usb_port].val = val;

	/* Mux is not powered in Z1 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (mux_state == USB_PD_MUX_NONE) ? EC_SUCCESS :
							EC_ERROR_NOT_POWERED;

	saved_mux_state[me->usb_port].write_pending = true;
	amd_fp6_set_mux_retry();

	return EC_SUCCESS;
}

static int amd_fp6_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	uint8_t val;
	bool inverted;
	uint8_t mode;

	/* Mux is not powered in Z1 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return USB_PD_MUX_NONE;

	RETURN_ERROR(amd_fp6_mux_port0_read(me, &val));

	mode = (val & AMD_FP6_MUX_MODE_MASK);
	inverted = !!(val & AMD_FP6_MUX_ORIENTATION);

	if (mode == AMD_FP6_MUX_MODE_USB)
		*mux_state = USB_PD_MUX_USB_ENABLED;
	else if (mode == AMD_FP6_MUX_MODE_DP)
		*mux_state = USB_PD_MUX_DP_ENABLED;
	else if (mode == AMD_FP6_MUX_MODE_DOCK)
		*mux_state = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED;
	else /* AMD_FP6_MUX_MODE_SAFE */
		*mux_state = USB_PD_MUX_NONE;

	if (inverted)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

/*
 * The FP6 USB Mux will not be ready for writing until *sometime* after S0.
 */
static void amd_fp6_chipset_resume(void)
{
	for (int i = 0; i < ARRAY_SIZE(saved_mux_state); i++)
		saved_mux_state[i].write_pending = true;
	hook_call_deferred(&amd_fp6_set_mux_retry_data,
			   CMD_RETRY_INTERVAL_MS * MSEC);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, amd_fp6_chipset_resume, HOOK_PRIO_DEFAULT);

static int amd_fp6_chipset_reset(const struct usb_mux *me)
{
	amd_fp6_chipset_resume();
	return EC_SUCCESS;
}

const struct usb_mux_driver amd_fp6_usb_mux_driver = {
	.set = &amd_fp6_set_mux,
	.get = &amd_fp6_get_mux,
	.chipset_reset = &amd_fp6_chipset_reset
};
