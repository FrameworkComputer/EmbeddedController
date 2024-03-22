/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "gpio.h"
#include "i2c.h"
#include "ioexpander.h"
#include "system.h"
#include "tca64xxa.h"

/*
 * This chip series contain registers in the same order.
 * Difference between models is only amount of registers and
 * value of which you must multiply to access specific register.
 * For 16 bit series, registers are 2 byte away, so to access TCA64XXA_REG_CONF
 * you must multiply it by 2. For 24 bit, they are away by 4 bytes so you
 * must multiply them by 4. Flags value contains information which version
 * of chip is used.
 */
#define TCA64XXA_PORT_ID(port, reg, flags)                                  \
	((((flags) & TCA64XXA_FLAG_VER_MASK) >> TCA64XXA_FLAG_VER_OFFSET) * \
		 (reg) +                                                    \
	 (port))

static int tca64xxa_write_byte(int ioex, int port, int reg, uint8_t val)
{
	const struct ioexpander_config_t *ioex_p = &ioex_config[ioex];
	const int reg_addr = TCA64XXA_PORT_ID(
		port, reg,
		(ioex_p->flags & IOEX_FLAGS_TCA64XXA_FLAG_VER_TCA6416A) ? 2 :
									  4);

	return i2c_write8(ioex_p->i2c_host_port, ioex_p->i2c_addr_flags,
			  reg_addr, val);
}

static int tca64xxa_read_byte(int ioex, int port, int reg, int *val)
{
	const struct ioexpander_config_t *ioex_p = &ioex_config[ioex];
	const int reg_addr = TCA64XXA_PORT_ID(
		port, reg,
		(ioex_p->flags & IOEX_FLAGS_TCA64XXA_FLAG_VER_TCA6416A) ? 2 :
									  4);

	return i2c_read8(ioex_p->i2c_host_port, ioex_p->i2c_addr_flags,
			 reg_addr, val);
}

/* Restore default values in registers */
static int tca64xxa_reset(int ioex, int portsCount)
{
	int port;
	int ret;

	/*
	 * On servo_v4p1, reset pin is pulled up and it results in values
	 * not being restored to default ones after software reboot.
	 * This loop sets default values (from specification) to all registers.
	 */
	for (port = 0; port < portsCount; port++) {
		ret = tca64xxa_write_byte(ioex, port, TCA64XXA_REG_OUTPUT,
					  TCA64XXA_DEFAULT_OUTPUT);
		if (ret)
			return ret;

		ret = tca64xxa_write_byte(ioex, port, TCA64XXA_REG_POLARITY_INV,
					  TCA64XXA_DEFAULT_POLARITY_INV);
		if (ret)
			return ret;

		ret = tca64xxa_write_byte(ioex, port, TCA64XXA_REG_CONF,
					  TCA64XXA_DEFAULT_CONF);
		if (ret)
			return ret;
	}

	return EC_SUCCESS;
}

/* Initialize IO expander chip/driver */
static int tca64xxa_init(int ioex)
{
	const struct ioexpander_config_t *ioex_p = &ioex_config[ioex];
	int portsCount;

	if (ioex_p->flags & IOEX_FLAGS_TCA64XXA_FLAG_VER_TCA6416A)
		portsCount = 2;
	else if (ioex_p->flags & IOEX_FLAGS_TCA64XXA_FLAG_VER_TCA6424A)
		portsCount = 3;
	else
		return EC_ERROR_UNIMPLEMENTED;

	if (!system_jumped_late())
		return tca64xxa_reset(ioex, portsCount);

	return EC_SUCCESS;
}

/* Get the current level of the IOEX pin */
static int tca64xxa_get_level(int ioex, int port, int mask, int *val)
{
	int buf;
	int ret;

	ret = tca64xxa_read_byte(ioex, port, TCA64XXA_REG_INPUT, &buf);
	*val = !!(buf & mask);

	return ret;
}

/* Set the level of the IOEX pin */
static int tca64xxa_set_level(int ioex, int port, int mask, int val)
{
	int ret;
	int v;

	ret = tca64xxa_read_byte(ioex, port, TCA64XXA_REG_OUTPUT, &v);
	if (ret)
		return ret;

	if (val)
		v |= mask;
	else
		v &= ~mask;

	return tca64xxa_write_byte(ioex, port, TCA64XXA_REG_OUTPUT, v);
}

/* Get flags for the IOEX pin */
static int tca64xxa_get_flags_by_mask(int ioex, int port, int mask, int *flags)
{
	int ret;
	int v;

	ret = tca64xxa_read_byte(ioex, port, TCA64XXA_REG_CONF, &v);
	if (ret)
		return ret;

	*flags = 0;
	if (v & mask) {
		*flags |= GPIO_INPUT;
	} else {
		*flags |= GPIO_OUTPUT;

		ret = tca64xxa_read_byte(ioex, port, TCA64XXA_REG_OUTPUT, &v);
		if (ret)
			return ret;

		if (v & mask)
			*flags |= GPIO_HIGH;
		else
			*flags |= GPIO_LOW;
	}

	return EC_SUCCESS;
}

/* Set flags for the IOEX pin */
static int tca64xxa_set_flags_by_mask(int ioex, int port, int mask, int flags)
{
	int ret;
	int v;

	/* Output value */
	if (flags & GPIO_OUTPUT) {
		ret = tca64xxa_read_byte(ioex, port, TCA64XXA_REG_OUTPUT, &v);
		if (ret)
			return ret;

		if (flags & GPIO_LOW)
			v &= ~mask;
		else if (flags & GPIO_HIGH)
			v |= mask;
		else
			return EC_ERROR_INVAL;

		ret = tca64xxa_write_byte(ioex, port, TCA64XXA_REG_OUTPUT, v);
		if (ret)
			return ret;
	}

	/* Configuration */
	ret = tca64xxa_read_byte(ioex, port, TCA64XXA_REG_CONF, &v);
	if (ret)
		return ret;

	if (flags & GPIO_INPUT)
		v |= mask;
	else if (flags & GPIO_OUTPUT)
		v &= ~mask;
	else
		return EC_ERROR_INVAL;

	ret = tca64xxa_write_byte(ioex, port, TCA64XXA_REG_CONF, v);
	if (ret)
		return ret;

	return EC_SUCCESS;
}

#ifdef CONFIG_IO_EXPANDER_SUPPORT_GET_PORT

/* Read levels for whole IO expander port */
static int tca64xxa_get_port(int ioex, int port, int *val)
{
	return tca64xxa_read_byte(ioex, port, TCA64XXA_REG_INPUT, val);
}

#endif

/* Driver structure */
const struct ioexpander_drv tca64xxa_ioexpander_drv = {
	.init = tca64xxa_init,
	.get_level = tca64xxa_get_level,
	.set_level = tca64xxa_set_level,
	.get_flags_by_mask = tca64xxa_get_flags_by_mask,
	.set_flags_by_mask = tca64xxa_set_flags_by_mask,
	.enable_interrupt = NULL,
#ifdef CONFIG_IO_EXPANDER_SUPPORT_GET_PORT
	.get_port = tca64xxa_get_port,
#endif
};
