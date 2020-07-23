/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * AMD FP5 USB/DP Mux.
 */

#include "amd_fp5.h"
#include "chipset.h"
#include "common.h"
#include "hooks.h"
#include "i2c.h"
#include "queue.h"
#include "timer.h"
#include "usb_mux.h"

static mux_state_t saved_mux_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static inline int amd_fp5_mux_read(const struct usb_mux *me, uint8_t *val)
{
	uint8_t buf[3] = { 0 };
	int rv;

	rv = i2c_xfer(me->i2c_port, me->i2c_addr_flags,
		      NULL, 0, buf, 3);
	if (rv)
		return rv;

	*val = buf[me->usb_port + 1];

	return EC_SUCCESS;
}

static inline int amd_fp5_mux_write(const struct usb_mux *me, uint8_t val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags,
			  me->usb_port, val);
}

static int amd_fp5_init(const struct usb_mux *me)
{
	return EC_SUCCESS;
}

static int amd_fp5_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	uint8_t val = 0;

	saved_mux_state[me->usb_port] = mux_state;

	/*
	 * This MUX is on the FP5 SoC.  If that device is not powered then
	 * we either have to complain that it is not powered or if we were
	 * setting the state to OFF, then go ahead and report that we did
	 * it because a powered down MUX is off.
	 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return (mux_state == USB_PD_MUX_NONE)
			? EC_SUCCESS
			: EC_ERROR_NOT_POWERED;

	if ((mux_state & USB_PD_MUX_USB_ENABLED) &&
		(mux_state & USB_PD_MUX_DP_ENABLED))
		val = (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_DOCK_INVERTED : AMD_FP5_MUX_DOCK;
	else if (mux_state & USB_PD_MUX_USB_ENABLED)
		val = (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_USB_INVERTED : AMD_FP5_MUX_USB;
	else if (mux_state & USB_PD_MUX_DP_ENABLED)
		val = (mux_state & USB_PD_MUX_POLARITY_INVERTED)
			? AMD_FP5_MUX_DP_INVERTED : AMD_FP5_MUX_DP;

	return amd_fp5_mux_write(me, val);
}

static int amd_fp5_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	uint8_t val = AMD_FP5_MUX_SAFE;

	/*
	 * This MUX is on the FP5 SoC.  Only access the device if we
	 * have power.  If that device is not powered then claim the
	 * state to be NONE, which is SAFE.
	 */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		int rv;

		rv = amd_fp5_mux_read(me, &val);
		if (rv)
			return rv;
	}

	switch (val) {
	case AMD_FP5_MUX_USB:
		*mux_state = USB_PD_MUX_USB_ENABLED;
		break;
	case AMD_FP5_MUX_USB_INVERTED:
		*mux_state = USB_PD_MUX_USB_ENABLED |
				USB_PD_MUX_POLARITY_INVERTED;
		break;
	case AMD_FP5_MUX_DOCK:
		*mux_state = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED;
		break;
	case AMD_FP5_MUX_DOCK_INVERTED:
		*mux_state = USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED
			     | USB_PD_MUX_POLARITY_INVERTED;
		break;
	case AMD_FP5_MUX_DP:
		*mux_state = USB_PD_MUX_DP_ENABLED;
		break;
	case AMD_FP5_MUX_DP_INVERTED:
		*mux_state = USB_PD_MUX_DP_ENABLED |
				USB_PD_MUX_POLARITY_INVERTED;
		break;
	case AMD_FP5_MUX_SAFE:
	default:
		*mux_state = USB_PD_MUX_NONE;
		break;
	}

	return EC_SUCCESS;
}

static struct queue const chipset_reset_queue
	= QUEUE_NULL(CONFIG_USB_PD_PORT_MAX_COUNT, struct usb_mux *);

static void amd_fp5_chipset_reset_delay(void)
{
	struct usb_mux *me;
	int rv;

	while (queue_remove_unit(&chipset_reset_queue, &me)) {
		rv = amd_fp5_set_mux(me, saved_mux_state[me->usb_port]);
		if (rv)
			ccprints("C%d restore mux rv:%d", me->usb_port, rv);
	}
}
DECLARE_DEFERRED(amd_fp5_chipset_reset_delay);

/*
 * The AP's internal USB-C mux is reset when AP resets, so wait for
 * it to be ready and then restore the previous setting.
 */
static int amd_fp5_chipset_reset(const struct usb_mux *me)
{
	queue_add_unit(&chipset_reset_queue, &me);
	hook_call_deferred(&amd_fp5_chipset_reset_delay_data, 200 * MSEC);
	return EC_SUCCESS;
}

const struct usb_mux_driver amd_fp5_usb_mux_driver = {
	.init = &amd_fp5_init,
	.set = &amd_fp5_set_mux,
	.get = &amd_fp5_get_mux,
	.chipset_reset = &amd_fp5_chipset_reset,
};
