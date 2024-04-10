/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX3443: 10G Active Mux (6x4) with
 * Integrated Re-timers for USB3.2/DisplayPort
 */

#include "anx3443.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "time.h"
#include "usb_mux.h"
#include "util.h"

/*
 * Empirical testing found it takes ~12ms to wake mux.
 * Setting timeout to 20ms for some buffer.
 */
#define ANX3443_I2C_WAKE_TIMEOUT_MS 20
#define ANX3443_I2C_WAKE_RETRY_DELAY_US 500

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static struct {
	mux_state_t mux_state;
	bool awake;
} saved_mux_state[CONFIG_USB_PD_PORT_MAX_COUNT];

static inline int anx3443_read(const struct usb_mux *me, uint8_t reg, int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static inline int anx3443_write(const struct usb_mux *me, uint8_t reg,
				uint8_t val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static int anx3443_power_off(const struct usb_mux *me)
{
	/**
	 * No-op if the mux is already down.
	 *
	 * Writing or reading any register wakes the mux up.
	 */
	if (!saved_mux_state[me->usb_port].awake)
		return EC_SUCCESS;

	/*
	 * The mux will not send an acknowledgment when powered off, so ignore
	 * response and always return success.
	 */
	anx3443_write(me, ANX3443_REG_POWER_CNTRL, ANX3443_POWER_CNTRL_OFF);
	saved_mux_state[me->usb_port].awake = false;
	return EC_SUCCESS;
}

static int anx3443_wake_up(const struct usb_mux *me)
{
	timestamp_t start;
	int rv;
	int val;

	/* Keep reading top register until mux wakes up or timesout */
	start = get_time();
	do {
		rv = anx3443_read(me, 0x0, &val);
		if (!rv)
			break;
		crec_usleep(ANX3443_I2C_WAKE_RETRY_DELAY_US);
	} while (time_since32(start) < ANX3443_I2C_WAKE_TIMEOUT_MS * MSEC);
	if (rv) {
		CPRINTS("ANX3443: Failed to wake mux rv:%d", rv);
		return EC_ERROR_TIMEOUT;
	}

	/* ULTRA_LOW_POWER must always be disabled (Fig 2-2) */
	RETURN_ERROR(anx3443_write(me, ANX3443_REG_ULTRA_LOW_POWER,
				   ANX3443_ULTRA_LOW_POWER_DIS));
	saved_mux_state[me->usb_port].awake = true;

	return EC_SUCCESS;
}

static int anx3443_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			   bool *ack_required)
{
	int reg;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* This driver treats safe mode as none */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	saved_mux_state[me->usb_port].mux_state = mux_state;

	/* To disable both DP and USB the mux must be powered off. */
	if (!(mux_state & (USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED)))
		return anx3443_power_off(me);

	/**
	 * If the request state is not NONE, process it after we back to
	 * S0.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		return EC_SUCCESS;

	RETURN_ERROR(anx3443_wake_up(me));

	/* ULP_CFG_MODE_EN overrides pin control. Always set it */
	reg = ANX3443_ULP_CFG_MODE_EN;
	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= ANX3443_ULP_CFG_MODE_USB_EN;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= ANX3443_ULP_CFG_MODE_DP_EN;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= ANX3443_ULP_CFG_MODE_FLIP;

	return anx3443_write(me, ANX3443_REG_ULP_CFG_MODE, reg);
}

static int anx3443_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int reg;

	/* Mux is not powered in Z1 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return USB_PD_MUX_NONE;

	RETURN_ERROR(anx3443_wake_up(me));

	*mux_state = 0;
	RETURN_ERROR(anx3443_read(me, ANX3443_REG_ULP_CFG_MODE, &reg));

	if (reg & ANX3443_ULP_CFG_MODE_USB_EN)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & ANX3443_ULP_CFG_MODE_DP_EN)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & ANX3443_ULP_CFG_MODE_FLIP)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

static int anx3443_init(const struct usb_mux *me)
{
	uint64_t now;
	bool unused;

	/*
	 * ANX3443 requires 30ms to power on. EC and ANX3443 are on the same
	 * power rail, so just wait 30ms since EC boot.
	 */
	now = get_time().val;
	if (now < ANX3443_I2C_READY_DELAY)
		crec_usleep(ANX3443_I2C_READY_DELAY - now);

	RETURN_ERROR(anx3443_wake_up(me));

	/*
	 * Note that bypassing the usb_mux API is okay for internal driver calls
	 * since the task calling init already holds this port's mux lock.
	 */
	/* Default to USB mode */
	RETURN_ERROR(anx3443_set_mux(me, USB_PD_MUX_USB_ENABLED, &unused));

	return EC_SUCCESS;
}

const struct usb_mux_driver anx3443_usb_mux_driver = {
	.init = anx3443_init,
	.set = anx3443_set_mux,
	.get = anx3443_get_mux,
};

static bool anx3443_port_is_usb2_only(const struct usb_mux *me)
{
	int val;
	int port = me->usb_port;

	if (!(saved_mux_state[port].mux_state & USB_PD_MUX_USB_ENABLED))
		return false;

	if (anx3443_read(me, ANX3443_REG_USB_STATUS, &val))
		return false;

	return !(val & ANX3443_UP_EN_RTERM_ST);
}

static void anx3443_suspend(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		const struct usb_mux *mux = usb_muxes[i].mux;

		if (mux->driver != &anx3443_usb_mux_driver)
			continue;

		if (anx3443_port_is_usb2_only(mux))
			anx3443_power_off(mux);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, anx3443_suspend, HOOK_PRIO_DEFAULT);

static void anx3443_resume(void)
{
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		int port = usb_muxes[i].mux->usb_port;
		bool ack_required;

		if (usb_muxes[i].mux->driver != &anx3443_usb_mux_driver)
			continue;

		anx3443_set_mux(usb_muxes[i].mux,
				saved_mux_state[port].mux_state, &ack_required);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, anx3443_resume, HOOK_PRIO_DEFAULT);
