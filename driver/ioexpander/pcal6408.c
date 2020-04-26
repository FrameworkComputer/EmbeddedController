/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NXP PCA(L)6408 I/O expander
 */
#include "common.h"
#include "console.h"
#include "math_util.h"
#include "gpio.h"
#include "i2c.h"
#include "ioexpander.h"
#include "pcal6408.h"

#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)

/*
 * Store interrupt mask registers locally. In this way,
 * we don't have to read it via i2c transaction every time.
 * Default value of interrupt mask register is 0xff.
 */
uint8_t pcal6408_int_mask[] = {
	[0 ... (CONFIG_IO_EXPANDER_PORT_COUNT - 1)] =  0xff };


static int pcal6408_read(int ioex, int reg, int *data)
{
	int rv;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	rv = i2c_read8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
			reg, data);

	return rv;
}

static int pcal6408_write(int ioex, int reg, int data)
{
	int rv;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	rv = i2c_write8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
			reg, data);

	return rv;
}

static int pcal6408_ioex_check_is_valid(int port, int mask)
{
	if (port != 0)
		return EC_ERROR_INVAL;

	if (mask & ~PCAL6408_VALID_GPIO_MASK) {
		CPRINTF("GPIO%02d is not support in PCAL6408\n",
						__fls(mask));
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static int pcal6408_ioex_init(int ioex)
{
	/* It seems that we have nothing to do here.
	 * This chip has not a chip id to be identified.
	 */
	return EC_SUCCESS;
}

static int pcal6408_ioex_get_level(int ioex, int port, int mask, int *val)
{
	int rv;

	rv = pcal6408_ioex_check_is_valid(port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	rv = pcal6408_read(ioex, PCAL6408_REG_INPUT, val);
	if (rv != EC_SUCCESS)
		return rv;

	*val = !!(*val & mask);

	return EC_SUCCESS;
}

static int pcal6408_ioex_set_level(int ioex, int port, int mask, int value)
{
	int rv, val;

	rv = pcal6408_ioex_check_is_valid(port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	rv = pcal6408_read(ioex, PCAL6408_REG_OUTPUT, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (value)
		val |= mask;
	else
		val &= ~mask;

	return pcal6408_write(ioex, PCAL6408_REG_OUTPUT, val);
}

static int pcal6408_ioex_get_flags_by_mask(int ioex, int port, int mask,
					int *flags)
{
	int rv, val;

	rv = pcal6408_ioex_check_is_valid(port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	*flags = GPIO_FLAG_NONE;

	rv = pcal6408_read(ioex, PCAL6408_REG_CONFIG, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (val & mask)
		*flags |= GPIO_INPUT;
	else
		*flags |= GPIO_OUTPUT;

	rv = pcal6408_read(ioex, PCAL6408_REG_INPUT, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (val & mask)
		*flags |= GPIO_HIGH;
	else
		*flags |= GPIO_LOW;

	rv = pcal6408_read(ioex, PCAL6408_REG_OUT_CONFIG, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (val & PCAL6408_OUT_CONFIG_OPEN_DRAIN)
		*flags |= GPIO_OPEN_DRAIN;

	rv = pcal6408_read(ioex, PCAL6408_REG_PULL_ENABLE, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (val & mask) {
		rv = pcal6408_read(ioex, PCAL6408_REG_PULL_UP_DOWN, &val);
		if (rv != EC_SUCCESS)
			return rv;

		if (val & mask)
			*flags |= GPIO_PULL_UP;
		else
			*flags |= GPIO_PULL_DOWN;
	}

	rv = pcal6408_read(ioex, PCAL6408_REG_INT_MASK, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if ((!!(val & mask) == 0) && ((*flags) & GPIO_INPUT))
		*flags |= GPIO_INT_BOTH;

	return rv;
}

static int pcal6408_ioex_set_flags_by_mask(int ioex, int port, int mask,
					int flags)
{
	int rv, val;

	rv = pcal6408_ioex_check_is_valid(port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	if (((flags & GPIO_INT_BOTH) == GPIO_INT_RISING) ||
		((flags & GPIO_INT_BOTH) == GPIO_INT_FALLING)) {
		CPRINTF("PCAL6408 only support GPIO_INT_BOTH.\n");
		return EC_ERROR_INVAL;
	}


	if ((flags & (GPIO_INT_F_RISING | GPIO_INT_F_FALLING)) &&
		!(flags & GPIO_INPUT)) {
		CPRINTF("Interrupt pin must be GPIO_INPUT.\n");
		return EC_ERROR_INVAL;
	}

	/* All output gpios share GPIO_OPEN_DRAIN, should be consistent */
	if (flags & GPIO_OPEN_DRAIN)
		val = PCAL6408_OUT_CONFIG_OPEN_DRAIN;
	else
		val = 0;

	rv = pcal6408_write(ioex, PCAL6408_REG_OUT_CONFIG, val);
	if (rv != EC_SUCCESS)
		return rv;

	rv = pcal6408_read(ioex, PCAL6408_REG_CONFIG, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (flags & GPIO_INPUT)
		val |= mask;
	if (flags & GPIO_OUTPUT)
		val &= ~mask;

	rv = pcal6408_write(ioex, PCAL6408_REG_CONFIG, val);
	if (rv != EC_SUCCESS)
		return rv;

	if (flags & GPIO_OUTPUT) {
		rv = pcal6408_read(ioex, PCAL6408_REG_OUTPUT, &val);
		if (rv != EC_SUCCESS)
			return rv;

		if (flags & GPIO_HIGH)
			val |= mask;
		else if (flags & GPIO_LOW)
			val &= ~mask;

		rv = pcal6408_write(ioex, PCAL6408_REG_OUTPUT, val);
		if (rv != EC_SUCCESS)
			return rv;
	}

	if (!(flags & (GPIO_PULL_UP | GPIO_PULL_DOWN))) {
		rv = pcal6408_read(ioex, PCAL6408_REG_PULL_ENABLE, &val);
		if (rv != EC_SUCCESS)
			return rv;

		val &= ~mask;

		rv = pcal6408_write(ioex, PCAL6408_REG_PULL_ENABLE, val);
		if (rv != EC_SUCCESS)
			return rv;
	} else {
		rv = pcal6408_read(ioex, PCAL6408_REG_PULL_ENABLE, &val);
		if (rv != EC_SUCCESS)
			return rv;

		val |= mask;

		rv = pcal6408_write(ioex, PCAL6408_REG_PULL_ENABLE, val);
		if (rv != EC_SUCCESS)
			return rv;

		rv = pcal6408_read(ioex, PCAL6408_REG_PULL_UP_DOWN, &val);
		if (rv != EC_SUCCESS)
			return rv;

		if (flags & GPIO_PULL_UP)
			val |= mask;
		else if (flags & GPIO_PULL_DOWN)
			val &= ~mask;

		rv = pcal6408_write(ioex, PCAL6408_REG_PULL_UP_DOWN, val);
		if (rv != EC_SUCCESS)
			return rv;
	}

	return rv;
}

static int pcal6408_ioex_enable_interrupt(int ioex, int port, int mask,
					int enable)
{
	int rv, val;

	rv = pcal6408_ioex_check_is_valid(port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	/* Interrupt should be latched */
	rv = pcal6408_read(ioex, PCAL6408_REG_INPUT_LATCH, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (enable)
		val |= mask;
	else
		val &= ~mask;

	rv = pcal6408_write(ioex, PCAL6408_REG_INPUT_LATCH, val);
	if (rv != EC_SUCCESS)
		return rv;

	/*
	 * Enable or disable interrupt.
	 * In PCAL6408_REG_INT_MASK, 0 = enable interrupt,
	 *                           1 = disable interrupt.
	 */
	if (enable)
		pcal6408_int_mask[ioex] &= ~mask;
	else
		pcal6408_int_mask[ioex] |= mask;

	rv = pcal6408_write(ioex, PCAL6408_REG_INT_MASK,
			pcal6408_int_mask[ioex]);

	return rv;
}

int pcal6408_ioex_event_handler(int ioex)
{
	int int_status, int_mask;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];
	int i, rv = 0;
	const struct ioex_info *g;

	int_mask = pcal6408_int_mask[ioex];

	/*
	 * Read input port register will clear the interrupt,
	 * read status register will not.
	 */
	rv = i2c_read8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
				PCAL6408_REG_INT_STATUS, &int_status);
	if (rv != EC_SUCCESS)
		return rv;

	/*
	 * In pcal6408_int_mask[x], 0 = enable interrupt,
	 *                          1 = disable interrupt.
	 */
	int_status = int_status & ~int_mask;

	if (!int_status)
		return EC_SUCCESS;

	for (i = 0, g = ioex_list; i < ioex_ih_count; i++, g++) {

		if (ioex == g->ioex && 0 == g->port &&
					(int_status & g->mask)) {
			ioex_irq_handlers[i](i);
			int_status &= ~g->mask;
			if (!int_status)
				break;
		}
	}

	return EC_SUCCESS;
}

const struct ioexpander_drv pcal6408_ioexpander_drv = {
	.init			= &pcal6408_ioex_init,
	.get_level		= &pcal6408_ioex_get_level,
	.set_level		= &pcal6408_ioex_set_level,
	.get_flags_by_mask	= &pcal6408_ioex_get_flags_by_mask,
	.set_flags_by_mask	= &pcal6408_ioex_set_flags_by_mask,
	.enable_interrupt	= &pcal6408_ioex_enable_interrupt,
};
