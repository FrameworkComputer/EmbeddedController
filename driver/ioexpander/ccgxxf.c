/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cypress CCGXXF I/O Port expander (built inside PD chip) driver source
 */

#include "console.h"
#include "i2c.h"
#include "ioexpander.h"

/* Add after all include files */
#include "ccgxxf.h"

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)

#ifdef CONFIG_IO_EXPANDER_SUPPORT_GET_PORT
#error "This driver doesn't support get_port function"
#endif

static inline int ccgxxf_read8(int ioex, int reg, int *data)
{
	return i2c_read8(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_addr_flags, reg, data);
}

static inline int ccgxxf_update8(int ioex, int reg, uint8_t mask,
				enum mask_update_action action)
{
	return i2c_update8(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_addr_flags, reg, mask, action);
}

static inline int ccgxxf_write16(int ioex, uint16_t reg, uint16_t data)
{
	return i2c_write16(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_addr_flags, reg, data);
}

static int ccgxxf_get_level(int ioex, int port, int mask, int *val)
{
	int rv;

	rv = ccgxxf_read8(ioex, CCGXXF_REG_GPIO_STATUS(port), val);
	if (!rv)
		*val = !!(*val & mask);

	return rv;
}

static int ccgxxf_set_level(int ioex, int port, int mask, int val)
{
	return ccgxxf_update8(ioex, CCGXXF_REG_GPIO_CONTROL(port), mask, val);
}

/*
 * Following type of pins are supported
 * - Output pins are supported with open-drain & pull-up
 * - Input pins are supported with pull-up & pull-down
 * - Analog pins
 * - 1.8V level GPIOs are supported per port and outputs can only be
 *   open-drain pins
 */
static int ccgxxf_set_flags_by_mask(int ioex, int port, int mask, int flags)
{
	uint16_t pin_mode;
	int rv;

	/* Push-pull output can't be configured for 1.8V level */
	if ((flags & GPIO_OUTPUT) && (flags & GPIO_SEL_1P8V) &&
		!(flags & GPIO_OPEN_DRAIN)) {
		CPRINTS("Invalid flags: ioex=%d, port=%d, mask=%d, flags=0x%x",
				ioex, port, mask, flags);

		return EC_ERROR_INVAL;
	}

	if (flags & GPIO_OUTPUT) {
		if (flags & GPIO_OPEN_DRAIN) {
			if (flags & GPIO_PULL_UP)
				pin_mode = CCGXXF_GPIO_MODE_RES_UP;
			else
				pin_mode = CCGXXF_GPIO_MODE_OD_LOW;
		} else {
			pin_mode = CCGXXF_GPIO_MODE_STRONG;
		}
	} else if (flags & GPIO_INPUT) {
		if (flags & GPIO_PULL_UP) {
			pin_mode = CCGXXF_GPIO_MODE_RES_UP;
			flags |= GPIO_HIGH;
		} else if (flags & GPIO_PULL_DOWN) {
			pin_mode = CCGXXF_GPIO_MODE_RES_DWN;
			flags |= GPIO_LOW;
		} else {
			pin_mode = CCGXXF_GPIO_MODE_HIZ_DIGITAL;
		}
	} else if (flags & GPIO_ANALOG) {
		pin_mode = CCGXXF_GPIO_MODE_HIZ_ANALOG;
	} else {
		return EC_ERROR_INVAL;
	}

	pin_mode = port | (pin_mode << CCGXXF_GPIO_PIN_MODE_SHIFT) |
			(mask << CCGXXF_GPIO_PIN_MASK_SHIFT);

	/* Note: once set the 1.8V level affect whole GPIO port */
	if (flags & GPIO_SEL_1P8V)
		pin_mode |= CCGXXF_GPIO_1P8V_SEL;

	/*
	 * Before setting the GPIO mode, initilaize the pins to default value
	 * to avoid spike on pins.
	 */
	if (flags & (GPIO_HIGH | GPIO_LOW)) {
		rv = ccgxxf_set_level(ioex, port, mask,
					flags & GPIO_HIGH ? 1 : 0);
		if (rv)
			return rv;
	}

	return	ccgxxf_write16(ioex, CCGXXF_REG_GPIO_MODE, pin_mode);
}

static int ccgxxf_get_flags_by_mask(int ioex, int port, int mask, int *flags)
{
	 /* TODO: Add it after implementing in the CCGXXF firmware. */
	return EC_SUCCESS;
}

static int ccgxxf_enable_interrupt(int ioex, int port, int mask, int enable)
{
	/* CCGXXF doesn't have interrupt capability on I/O expnader pins */
	return EC_ERROR_UNIMPLEMENTED;
}

int ccgxxf_init(int ioex)
{
	/* TCPC init of CCGXXF should handle initialization */
	return EC_SUCCESS;
}

const struct ioexpander_drv ccgxxf_ioexpander_drv = {
	.init			= &ccgxxf_init,
	.get_level		= &ccgxxf_get_level,
	.set_level		= &ccgxxf_set_level,
	.get_flags_by_mask	= &ccgxxf_get_flags_by_mask,
	.set_flags_by_mask	= &ccgxxf_set_flags_by_mask,
	.enable_interrupt	= &ccgxxf_enable_interrupt,
};
