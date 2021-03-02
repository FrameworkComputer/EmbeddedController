/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX3443: 10G Active Mux (6x4) with
 * Integrated Re-timers for USB3.2/DisplayPort
 */

#include "anx3443.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "time.h"
#include "usb_mux.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

static inline int anx3443_read(const struct usb_mux *me,
			       uint8_t reg, int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static inline int anx3443_write(const struct usb_mux *me,
				uint8_t reg, uint8_t val)
{
	return i2c_write8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static int anx3443_set_mux(const struct usb_mux *me, mux_state_t mux_state)
{
	int reg;

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

	/*
	 * ANX3443 requires 30ms to power on. EC and ANX3443 are on the same
	 * power rail, so just wait 30ms since EC boot.
	 */
	now = get_time().val;
	if (now < ANX3443_I2C_READY_DELAY)
		usleep(ANX3443_I2C_READY_DELAY - now);

	/* Disable ultra-low power mode  */
	RETURN_ERROR(anx3443_write(me, ANX3443_REG_ULTRA_LOW_POWER,
				   ANX3443_ULTRA_LOW_POWER_DIS));

	/* Start mux in safe mode */
	RETURN_ERROR(anx3443_set_mux(me, USB_PD_MUX_NONE));

	return EC_SUCCESS;
}

static int anx3443_enter_low_power_mode(const struct usb_mux *me)
{
	/* Enable vendor-defined ultra-low power mode */
	return anx3443_write(me, ANX3443_REG_ULTRA_LOW_POWER,
			     ANX3443_ULTRA_LOW_POWER_EN);
}

const struct usb_mux_driver anx3443_usb_mux_driver = {
	.init = anx3443_init,
	.set = anx3443_set_mux,
	.get = anx3443_get_mux,
	.enter_low_power_mode = anx3443_enter_low_power_mode,
};
