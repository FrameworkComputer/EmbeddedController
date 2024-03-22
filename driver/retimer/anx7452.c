/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ANX7452: Active redriver with linear equalisation
 */

#include "anx7452.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "retimer/anx7452_public.h"
#include "timer.h"
#include "usb_mux.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

static bool is_not_powered(void)
{
	return chipset_in_state(CHIPSET_STATE_HARD_OFF) != 0;
}

static int anx7452_top_read(const struct usb_mux *me, uint8_t reg, int *val)
{
	return i2c_read8(me->i2c_port, me->i2c_addr_flags, reg, val);
}

static int anx7452_top_update(const struct usb_mux *me, uint8_t reg,
			      uint8_t mask, uint8_t val)
{
	return i2c_field_update8(me->i2c_port, me->i2c_addr_flags, reg, mask,
				 val & mask);
}

static int anx7452_ctltop_update(const struct usb_mux *me, uint8_t reg,
				 uint8_t mask, uint8_t val)
{
	return i2c_field_update8(me->i2c_port, ANX7452_I2C_ADDR_CTLTOP_FLAGS,
				 reg, mask, val & mask);
}

static int anx7452_ctltop_update_all(const struct usb_mux *me, uint8_t cfg0_val,
				     uint8_t cfg1_val, uint8_t cfg2_val)
{
	RETURN_ERROR(anx7452_ctltop_update(me, ANX7452_CTLTOP_CFG0_REG,
					   ANX7452_CTLTOP_CFG0_REG_BIT_MASK,
					   cfg0_val));
	RETURN_ERROR(anx7452_ctltop_update(me, ANX7452_CTLTOP_CFG1_REG,
					   ANX7452_CTLTOP_CFG1_REG_BIT_MASK,
					   cfg1_val));
	RETURN_ERROR(anx7452_ctltop_update(me, ANX7452_CTLTOP_CFG2_REG,
					   ANX7452_CTLTOP_CFG2_REG_BIT_MASK,
					   cfg2_val));
	return EC_SUCCESS;
}

static int anx7452_power_enable(const struct usb_mux *me)
{
	int usb_enable;

	usb_enable = anx7452_controls[me->usb_port].usb_enable_gpio;

	if (is_not_powered()) {
		return EC_ERROR_NOT_POWERED;
	}

	gpio_set_level(usb_enable, 1);
	return EC_SUCCESS;
}

static int anx7452_i2c_awake(const struct usb_mux *me)
{
	int rv;
	timestamp_t start;

	/* Keep trying to update top register until mux wakes up or times
	 * out
	 */
	start = get_time();
	do {
		/* Configure for i2c control */
		rv = anx7452_top_update(
			me, ANX7452_TOP_STATUS_REG,
			ANX7452_TOP_STATUS_REG_I2C_CTRL_EN_BIT_MASK,
			ANX7452_TOP_REG_EN);
		if (rv == EC_SUCCESS) {
			break;
		}
		usleep(ANX7452_I2C_WAKE_RETRY_DELAY_US);
	} while (time_since32(start) < ANX7452_I2C_WAKE_TIMEOUT_MS * MSEC);
	if (rv != EC_SUCCESS) {
		CPRINTS("ANX7452: Failed to wake mux rv: %d", rv);
		return EC_ERROR_TIMEOUT;
	}

	return EC_SUCCESS;
}

static int anx7452_init(const struct usb_mux *me)
{
	RETURN_ERROR(anx7452_power_enable(me));

	msleep(ANX7452_I2C_PRE_WAKE_WAIT_MS);

	RETURN_ERROR(anx7452_i2c_awake(me));

	return EC_SUCCESS;
}

static int anx7452_set(const struct usb_mux *me, mux_state_t mux_state,
		       bool *ack_required)
{
	int cfg0_val = 0;
	int cfg1_val = 0;
	int cfg2_val = 0;

	if (is_not_powered()) {
		return EC_ERROR_NOT_POWERED;
	}

	/* This driver does not use host command ACKs */
	*ack_required = false;

	/* Apply CC polarity settings */
	if (mux_state & USB_PD_MUX_POLARITY_INVERTED) {
		cfg0_val |= ANX7452_CTLTOP_CFG0_FLIP_EN;
	}

	/* Apply DP enable settings */
	if (mux_state & USB_PD_MUX_DP_ENABLED) {
		cfg1_val |= ANX7452_CTLTOP_CFG1_DP_EN;
	}

	/* Apply USB3 enable settings */
	if (mux_state & USB_PD_MUX_USB_ENABLED) {
		cfg0_val |= ANX7452_CTLTOP_CFG0_USB3_EN;
	}

	/* Apply USB4 enable settings */
	if (mux_state & USB_PD_MUX_USB4_ENABLED) {
		cfg2_val |= ANX7452_CTLTOP_CFG2_USB4_EN;
	}

	/* Apply TBT compatible enable settings */
	if (mux_state & USB_PD_MUX_TBT_COMPAT_ENABLED) {
		cfg2_val |= ANX7452_CTLTOP_CFG2_TBT_EN;
	}

	RETURN_ERROR(
		anx7452_ctltop_update_all(me, cfg0_val, cfg1_val, cfg2_val));
	return EC_SUCCESS;
}

static int anx7452_get(const struct usb_mux *me, mux_state_t *mux_state)
{
	int reg_val = 0;

	if (is_not_powered()) {
		*mux_state = USB_PD_MUX_NONE;
		return EC_ERROR_NOT_POWERED;
	}

	RETURN_ERROR(anx7452_top_read(me, ANX7452_TOP_STATUS_REG, &reg_val));

	*mux_state = 0;
	if (reg_val & ANX7452_TOP_FLIP_INFO) {
		*mux_state |= USB_PD_MUX_POLARITY_INVERTED;
	}
	if (reg_val & ANX7452_TOP_DP_INFO) {
		*mux_state |= USB_PD_MUX_DP_ENABLED;
	}
	if (reg_val & ANX7452_TOP_TBT_INFO) {
		*mux_state |= USB_PD_MUX_TBT_COMPAT_ENABLED;
	}
	if (reg_val & ANX7452_TOP_USB3_INFO) {
		*mux_state |= USB_PD_MUX_USB_ENABLED;
	}
	if (reg_val & ANX7452_TOP_USB4_INFO) {
		*mux_state |= USB_PD_MUX_USB4_ENABLED;
	}

	return EC_SUCCESS;
}

const struct usb_mux_driver anx7452_usb_retimer_driver = {
	.init = anx7452_init,
	.set = anx7452_set,
	.get = anx7452_get,
};
