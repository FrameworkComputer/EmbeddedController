/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO expander for Nuvoton NCT38XX. */

#include "console.h"
#include "gpio.h"
#include "i2c.h"
#include "ioexpander.h"
#include "ioexpander_nct38xx.h"
#include "tcpci.h"

#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)

static int nct38xx_ioex_check_is_valid(int chip_info, int port, int mask)
{
	if (chip_info == NCT38XX_VARIANT_3808) {
		if (port == 1) {
			CPRINTF("Port 1 is not support in NCT3808\n");
			return EC_ERROR_INVAL;
		}
		if (mask & ~NCT38XXX_3808_VALID_GPIO_MASK) {

			CPRINTF("GPIO%02d is not support in NCT3808\n",
							__fls(mask));
			return EC_ERROR_INVAL;
		}
	}

	return EC_SUCCESS;

}

static int nct38xx_ioex_init(int ioex)
{
	int rv, val;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	/*
	 * Check the NCT38xx part number in the register DEVICE_ID[4:2]:
	 *  000: NCT3807
	 *  010: NCT3808
	 */
	rv = i2c_read8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
			TCPC_REG_BCD_DEV, &val);

	if (rv)
		CPRINTF("Failed to read NCT38XX DEV ID for IOexpander %d\n",
					ioex);
	else
		ioex_p->chip_info =
				((uint8_t)val & NCT38XX_VARIANT_MASK) >> 2;

	return rv;
}

static int nct38xx_ioex_get_level(int ioex, int port, int mask, int *val)
{
	int rv, reg;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	rv = nct38xx_ioex_check_is_valid(ioex_p->chip_info, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	reg = NCT38XXX_REG_GPIO_DATA_IN(port);
	rv = i2c_read8(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_slave_addr, reg, val);

	*val = !!(*val & mask);
	return rv;
}

static int nct38xx_ioex_set_level(int ioex, int port, int mask, int value)
{
	int rv, reg, val;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	rv = nct38xx_ioex_check_is_valid(ioex_p->chip_info, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	reg = NCT38XXX_REG_GPIO_DATA_OUT(port);

	rv = i2c_read8(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_slave_addr, reg, &val);

	if (value)
		val |= mask;
	else
		val &= ~mask;
	rv |= i2c_write8(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_slave_addr, reg, val);
	return rv;
}

static int nct38xx_ioex_get_flags(int ioex, int port, int mask, int *flags)
{
	int rv, reg, val, i2c_port, i2c_addr;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	i2c_port = ioex_p->i2c_host_port;
	i2c_addr = ioex_p->i2c_slave_addr;

	rv = nct38xx_ioex_check_is_valid(ioex_p->chip_info, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	reg = NCT38XXX_REG_GPIO_DIR(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (val & mask)
		*flags |= GPIO_OUTPUT;
	else
		*flags |= GPIO_INPUT;

	reg = NCT38XXX_REG_GPIO_DATA_IN(port);
	rv |= i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (val & mask)
		*flags |= GPIO_HIGH;
	else
		*flags |= GPIO_LOW;

	reg = NCT38XXX_REG_GPIO_OD_SEL(port);
	rv |= i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (val & mask)
		*flags |= GPIO_OPEN_DRAIN;

	return rv;
}

static int nct38xx_ioex_set_flags_by_mask(int ioex, int port, int mask,
					int flags)
{
	int rv, reg, val, i2c_port, i2c_addr;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	i2c_port = ioex_p->i2c_host_port;
	i2c_addr = ioex_p->i2c_slave_addr;

	rv = nct38xx_ioex_check_is_valid(ioex_p->chip_info, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	/*
	 * GPIO port 0 muxs with alternative function. Disable the alternative
	 * function before setting flags.
	 */
	if (port == 0) {
		/* GPIO03 in NCT3807 is not muxed with other function. */
		if (!(ioex_p->chip_info ==
					NCT38XX_VARIANT_3807 && mask & 0x08)) {
			reg = NCT38XXX_REG_MUX_CONTROL;
			rv |= i2c_read8(i2c_port, i2c_addr, reg, &val);
			val = (val | mask);
			rv |= i2c_write8(i2c_port, i2c_addr, reg, val);
		}
	}

	val = flags & ~NCT38XX_SUPPORT_GPIO_FLAGS;
	if (val) {
		CPRINTF("Flag 0x%08x is not supported\n", val);
		return EC_ERROR_INVAL;
	}

	/* Select open drain 0:push-pull 1:open-drain */
	reg = NCT38XXX_REG_GPIO_OD_SEL(port);
	rv |= i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (flags & GPIO_OPEN_DRAIN)
		val |= mask;
	else
		val &= ~mask;
	rv |= i2c_write8(i2c_port, i2c_addr, reg, val);

	/* Configure the output level */
	reg = NCT38XXX_REG_GPIO_DATA_OUT(port);
	rv |= i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (flags & GPIO_HIGH)
		val |= mask;
	else if (flags & GPIO_LOW)
		val &= ~mask;
	rv |= i2c_write8(i2c_port, i2c_addr, reg, val);

	reg = NCT38XXX_REG_GPIO_DIR(port);
	rv |= i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (flags & GPIO_OUTPUT)
		val |= mask;
	else
		val &= ~mask;
	rv |= i2c_write8(i2c_port, i2c_addr, reg, val);

	return rv;
}

const struct ioexpander_drv nct38xx_ioexpander_drv = {
	.init              = &nct38xx_ioex_init,
	.get_level         = &nct38xx_ioex_get_level,
	.set_level         = &nct38xx_ioex_set_level,
	.get_flags_by_mask = &nct38xx_ioex_get_flags,
	.set_flags_by_mask = &nct38xx_ioex_set_flags_by_mask,
};
