/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7451: 10G Active Mux (4x4) with
 * Integrated Re-timers for USB3.2/DisplayPort
 */

#include "anx7451.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "timer.h"
#include "usb_mux.h"
#include "util.h"

/*
 * Empirical testing found it takes ~12ms to wake mux.
 * Setting timeout to 20ms for some buffer.
 */
#define ANX7451_I2C_WAKE_TIMEOUT_MS 20
#define ANX7451_I2C_WAKE_RETRY_DELAY_US 500

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static inline int anx7451_read(const struct usb_mux *me, uint8_t reg, int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static inline int anx7451_write(const struct usb_mux *me, uint8_t reg,
				uint8_t val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static int anx7451_power_off(const struct usb_mux *me)
{
	/*
	 * The mux will not send an acknowledgment when powered off, so ignore
	 * response and always return success.
	 */
	anx7451_write(me, ANX7451_REG_POWER_CNTRL, ANX7451_POWER_CNTRL_OFF);
	return EC_SUCCESS;
}

static int anx7451_wake_up(const struct usb_mux *me)
{
	timestamp_t start;
	int rv;
	int val;
	uint16_t usb_i2c_addr = board_anx7451_get_usb_i2c_addr(me);

	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	/* Keep reading top register until mux wakes up or timesout */
	start = get_time();
	do {
		rv = anx7451_read(me, 0x0, &val);
		if (!rv)
			break;
		crec_usleep(ANX7451_I2C_WAKE_RETRY_DELAY_US);
	} while (time_since32(start) < ANX7451_I2C_WAKE_TIMEOUT_MS * MSEC);
	if (rv) {
		CPRINTS("ANX7451: Failed to wake mux rv:%d", rv);
		return EC_ERROR_TIMEOUT;
	}

	/* ULTRA_LOW_POWER must always be disabled (Fig 2-2) */
	RETURN_ERROR(anx7451_write(me, ANX7451_REG_ULTRA_LOW_POWER,
				   ANX7451_ULTRA_LOW_POWER_DIS));

	/*
	 * Configure ANX7451 USB I2C address.
	 * Shift 1 bit to make 7 bit address an 8 bit address.
	 */
	RETURN_ERROR(
		anx7451_write(me, ANX7451_REG_USB_I2C_ADDR, usb_i2c_addr << 1));

	/* b/185276137: Fix ANX7451 upstream AUX FLIP */
	RETURN_ERROR(i2c_write8(me->i2c_port, usb_i2c_addr,
				ANX7451_REG_USB_AUX_FLIP_CTRL,
				ANX7451_USB_AUX_FLIP_EN));

	return EC_SUCCESS;
}

static int anx7451_set_mux(const struct usb_mux *me, mux_state_t mux_state,
			   bool *ack_required)
{
	int reg;

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* This driver treats safe mode as none */
	if (mux_state & USB_PD_MUX_SAFE_MODE)
		mux_state = USB_PD_MUX_NONE;

	/*
	 * Mux is not powered in Z1, and will start up in USB mode.  Ensure any
	 * mux sets when off get run again so we don't leave the retimer on with
	 * the None mode set
	 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return EC_ERROR_NOT_POWERED;

	/* To disable both DP and USB the mux must be powered off. */
	if (!(mux_state & (USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED)))
		return anx7451_power_off(me);

	RETURN_ERROR(anx7451_wake_up(me));

	/* ULP_CFG_MODE_EN overrides pin control. Always set it */
	reg = ANX7451_ULP_CFG_MODE_EN;
	if (mux_state & USB_PD_MUX_USB_ENABLED)
		reg |= ANX7451_ULP_CFG_MODE_USB_EN;
	if (mux_state & USB_PD_MUX_DP_ENABLED)
		reg |= ANX7451_ULP_CFG_MODE_DP_EN;
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED)
		reg |= ANX7451_ULP_CFG_MODE_FLIP;

	return anx7451_write(me, ANX7451_REG_ULP_CFG_MODE, reg);
}

static int anx7451_get_mux(const struct usb_mux *me, mux_state_t *mux_state)
{
	int reg;

	/* Mux is not powered in Z1 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return USB_PD_MUX_NONE;

	RETURN_ERROR(anx7451_wake_up(me));

	*mux_state = 0;
	RETURN_ERROR(anx7451_read(me, ANX7451_REG_ULP_CFG_MODE, &reg));

	if (reg & ANX7451_ULP_CFG_MODE_USB_EN)
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	if (reg & ANX7451_ULP_CFG_MODE_DP_EN)
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	if (reg & ANX7451_ULP_CFG_MODE_FLIP)
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver anx7451_usb_mux_driver = {
	.set = anx7451_set_mux,
	.get = anx7451_get_mux,
	/* Low power mode is not supported on ANX7451 */
};
