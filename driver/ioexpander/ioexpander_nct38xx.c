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
#include "nct38xx.h"
#include "tcpci.h"

#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ## args)
#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)

/*
 * Store the GPIO_ALERT_MASK_0/1 and chip ID registers locally. In this way,
 * we don't have to read it via I2C transaction everytime.
 */
struct nct38xx_chip_data {
	uint8_t int_mask[2];
	int chip_id;
};

static struct nct38xx_chip_data chip_data[CONFIG_IO_EXPANDER_PORT_COUNT] = {
	 [0 ... (CONFIG_IO_EXPANDER_PORT_COUNT - 1)] =  { {0, 0}, -1 }
};

static int nct38xx_ioex_check_is_valid(int ioex, int port, int mask)
{
	if (chip_data[ioex].chip_id == NCT38XX_VARIANT_3808) {
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

	if (rv != EC_SUCCESS) {
		CPRINTF("Failed to read NCT38XX DEV ID for IOexpander %d\n",
					ioex);
		return rv;
	}

	chip_data[ioex].chip_id = ((uint8_t)val & NCT38XX_VARIANT_MASK) >> 2;

	/*
	 * NCT38XX uses the Vendor Define bit in the ALERT event to indicate
	 * that an IOEX IO's interrupt is triggered.
	 * Normally, The ALERT MASK for Vendor Define event should be set by
	 * the NCT38XX TCPCI driver's init function.
	 * However, it should be also set here if we want to test the interrupt
	 * function of IOEX when the NCT38XX TCPCI driver is not included.
	 */
	if (!IS_ENABLED(CONFIG_USB_PD_TCPM_NCT38XX)) {
		rv = i2c_write16(ioex_p->i2c_host_port,
				ioex_p->i2c_slave_addr, TCPC_REG_ALERT_MASK,
				TCPC_REG_ALERT_VENDOR_DEF);
		if (rv != EC_SUCCESS)
			return rv;
	}
	return EC_SUCCESS;
}

static int nct38xx_ioex_get_level(int ioex, int port, int mask, int *val)
{
	int rv, reg;

	rv = nct38xx_ioex_check_is_valid(ioex, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	reg = NCT38XX_REG_GPIO_DATA_IN(port);
	rv = i2c_read8(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_slave_addr, reg, val);
	if (rv != EC_SUCCESS)
		return rv;

	*val = !!(*val & mask);

	return EC_SUCCESS;
}

static int nct38xx_ioex_set_level(int ioex, int port, int mask, int value)
{
	int rv, reg, val;

	rv = nct38xx_ioex_check_is_valid(ioex, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	reg = NCT38XX_REG_GPIO_DATA_OUT(port);

	rv = i2c_read8(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_slave_addr, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (value)
		val |= mask;
	else
		val &= ~mask;

	return i2c_write8(ioex_config[ioex].i2c_host_port,
			ioex_config[ioex].i2c_slave_addr, reg, val);
}

static int nct38xx_ioex_get_flags(int ioex, int port, int mask, int *flags)
{
	int rv, reg, val, i2c_port, i2c_addr;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	i2c_port = ioex_p->i2c_host_port;
	i2c_addr = ioex_p->i2c_slave_addr;

	rv = nct38xx_ioex_check_is_valid(ioex, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	reg = NCT38XX_REG_GPIO_DIR(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (val & mask)
		*flags |= GPIO_OUTPUT;
	else
		*flags |= GPIO_INPUT;

	reg = NCT38XX_REG_GPIO_DATA_IN(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (val & mask)
		*flags |= GPIO_HIGH;
	else
		*flags |= GPIO_LOW;

	reg = NCT38XX_REG_GPIO_OD_SEL(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (val & mask)
		*flags |= GPIO_OPEN_DRAIN;

	return EC_SUCCESS;
}

static int nct38xx_ioex_sel_int_type(int i2c_port, int i2c_addr, int port,
					int mask, int flags)
{
	int rv;
	int reg_rising, reg_falling;
	int rising, falling;

	reg_rising = NCT38XX_REG_GPIO_ALERT_RISE(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg_rising, &rising);
	if (rv != EC_SUCCESS)
		return rv;

	reg_falling = NCT38XX_REG_GPIO_ALERT_FALL(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg_falling, &falling);
	if (rv != EC_SUCCESS)
		return rv;

	/* Handle interrupt for level trigger */
	if ((flags & GPIO_INT_F_HIGH) || (flags & GPIO_INT_F_LOW)) {
		int reg_level, level;

		reg_level = NCT38XX_REG_GPIO_ALERT_LEVEL(port);
		rv = i2c_read8(i2c_port, i2c_addr, reg_level, &level);
		if (rv != EC_SUCCESS)
			return rv;
		/*
		 * For "level" triggered interrupt, the related bit in
		 * ALERT_RISE and ALERT_FALL registers must be 0
		 */
		rising &= ~mask;
		falling &= ~mask;
		if (flags & GPIO_INT_F_HIGH)
			level |= mask;
		else
			level &= ~mask;

		rv = i2c_write8(i2c_port, i2c_addr, reg_rising, rising);
		if (rv != EC_SUCCESS)
			return rv;
		rv = i2c_write8(i2c_port, i2c_addr, reg_falling, falling);
		if (rv != EC_SUCCESS)
			return rv;
		rv = i2c_write8(i2c_port, i2c_addr, reg_level, level);
		if (rv != EC_SUCCESS)
			return rv;
	} else if ((flags & GPIO_INT_F_RISING) ||
				(flags & GPIO_INT_F_FALLING)) {
		if (flags & GPIO_INT_F_RISING)
			rising |= mask;
		else
			rising &= ~mask;
		if (flags & GPIO_INT_F_FALLING)
			falling |= mask;
		else
			falling &= ~mask;
		rv = i2c_write8(i2c_port, i2c_addr, reg_rising, rising);
		if (rv != EC_SUCCESS)
			return rv;
		rv = i2c_write8(i2c_port, i2c_addr, reg_falling, falling);
		if (rv != EC_SUCCESS)
			return rv;
	}
	return EC_SUCCESS;
}

static int nct38xx_ioex_set_flags_by_mask(int ioex, int port, int mask,
					int flags)
{
	int rv, reg, val, i2c_port, i2c_addr;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	i2c_port = ioex_p->i2c_host_port;
	i2c_addr = ioex_p->i2c_slave_addr;

	rv = nct38xx_ioex_check_is_valid(ioex, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	/*
	 * GPIO port 0 muxs with alternative function. Disable the alternative
	 * function before setting flags.
	 */
	if (port == 0) {
		/* GPIO03 in NCT3807 is not muxed with other function. */
		if (!(chip_data[ioex].chip_id ==
					NCT38XX_VARIANT_3807 && mask & 0x08)) {
			reg = NCT38XX_REG_MUX_CONTROL;
			rv = i2c_read8(i2c_port, i2c_addr, reg, &val);
			if (rv != EC_SUCCESS)
				return rv;

			val = (val | mask);
			rv = i2c_write8(i2c_port, i2c_addr, reg, val);
			if (rv != EC_SUCCESS)
				return rv;
		}
	}

	val = flags & ~NCT38XX_SUPPORT_GPIO_FLAGS;
	if (val) {
		CPRINTF("Flag 0x%08x is not supported\n", val);
		return EC_ERROR_INVAL;
	}

	/* Select open drain 0:push-pull 1:open-drain */
	reg = NCT38XX_REG_GPIO_OD_SEL(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (flags & GPIO_OPEN_DRAIN)
		val |= mask;
	else
		val &= ~mask;
	rv = i2c_write8(i2c_port, i2c_addr, reg, val);
	if (rv != EC_SUCCESS)
		return rv;

	nct38xx_ioex_sel_int_type(i2c_port, i2c_addr, port, mask, flags);

	/* Configure the output level */
	reg = NCT38XX_REG_GPIO_DATA_OUT(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (flags & GPIO_HIGH)
		val |= mask;
	else if (flags & GPIO_LOW)
		val &= ~mask;
	rv = i2c_write8(i2c_port, i2c_addr, reg, val);
	if (rv != EC_SUCCESS)
		return rv;

	reg = NCT38XX_REG_GPIO_DIR(port);
	rv = i2c_read8(i2c_port, i2c_addr, reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	if (flags & GPIO_OUTPUT)
		val |= mask;
	else
		val &= ~mask;

	return  i2c_write8(i2c_port, i2c_addr, reg, val);
}

/*
 * The following functions are used for IO's interrupt support.
 *
 * please note that if the system needs to use an IO on NCT38XX to support
 * the interrupt, the following two consideration should be taken into account.
 * 1. Interrupt latency:
 *    Because it requires to access the registers of NCT38XX via I2C
 *    transaction to know the interrupt event, there is some added latency
 *    for the interrupt handling. If the interrupt requires short latency,
 *    we do not recommend to connect such a signal to the NCT38XX.
 *
 * 2. Shared ALERT pin:
 *    Because the ALERT pin is shared also with the TCPC ALERT, we do not
 *    recommend to connect any signal that may generate a high rate of
 *    interrupts so it will not interfere with the normal work of the
 *    TCPC.
 */
static int nct38xx_ioex_enable_interrupt(int ioex, int port, int mask,
					int enable)
{
	int rv, reg, val;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	rv = nct38xx_ioex_check_is_valid(ioex, port, mask);
	if (rv != EC_SUCCESS)
		return rv;

	/* Clear the pending bit */
	reg = NCT38XX_REG_GPIO_ALERT_STAT(port);
	rv = i2c_read8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
					reg, &val);
	if (rv != EC_SUCCESS)
		return rv;

	val |= mask;
	rv = i2c_write8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
					reg, val);
	if (rv != EC_SUCCESS)
		return rv;

	reg = NCT38XX_REG_GPIO_ALERT_MASK(port);
	if (enable) {
		/* Enable the alert mask */
		chip_data[ioex].int_mask[port] |= mask;
		val = chip_data[ioex].int_mask[port];
	} else {
		/* Disable the alert mask */
		chip_data[ioex].int_mask[port] &= ~mask;
		val = chip_data[ioex].int_mask[port];
	}

	return i2c_write8(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
					reg, val);
}

int nct38xx_ioex_event_handler(int ioex)
{
	int reg, int_status, int_mask;
	int i, j, total_port;
	const struct ioex_info *g;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];
	int rv = 0;

	int_mask = chip_data[ioex].int_mask[0] | (
				chip_data[ioex].int_mask[1] << 8);
	reg = NCT38XX_REG_GPIO_ALERT_STAT(0);
	/*
	 * Read ALERT_STAT_0 and ALERT_STAT_1 register in a single I2C
	 * transaction to increase efficiency
	 */
	rv = i2c_read16(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
					reg, &int_status);
	if (rv != EC_SUCCESS)
		return rv;

	int_status = int_status & int_mask;
	/*
	 * Clear the changed status bits in ALERT_STAT_0 and ALERT_STAT_1
	 * register in a single I2C transaction to increase efficiency
	 */
	rv = i2c_write16(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
					reg, int_status);
	if (rv != EC_SUCCESS)
		return rv;

	/* For NCT3808, only check one port */
	total_port = (chip_data[ioex].chip_id == NCT38XX_VARIANT_3808) ?
		NCT38XX_NCT3808_MAX_IO_PORT :
		NCT38XX_NCT3807_MAX_IO_PORT;
	for (i = 0; i < total_port; i++) {
		uint8_t pending;

		pending = int_status >> (i * 8);

		if (!pending)
			continue;

		for (j = 0, g = ioex_list; j < ioex_ih_count; j++, g++) {

			if (ioex == g->ioex && i == g->port &&
						(pending & g->mask)) {
				ioex_irq_handlers[j](j);
				pending &= ~g->mask;
				if (!pending)
					break;
			}

		}
	}

	return EC_SUCCESS;
}

/*
 * Normally, the ALERT MASK for Vendor Define event should be checked by
 * the NCT38XX TCPCI driver's tcpc_alert function.
 * However, it should be checked here if we want to test the interrupt
 * function of IOEX when the NCT38XX TCPCI driver is not included.
 */
void nct38xx_ioex_handle_alert(int ioex)
{
	int rv, status;
	struct ioexpander_config_t *ioex_p = &ioex_config[ioex];

	rv = i2c_read16(ioex_p->i2c_host_port, ioex_p->i2c_slave_addr,
			TCPC_REG_ALERT, &status);
	if (rv != EC_SUCCESS)
		CPRINTF("fail to read ALERT register\n");

	if (status & TCPC_REG_ALERT_VENDOR_DEF) {
		rv = i2c_write16(ioex_p->i2c_host_port,
				ioex_p->i2c_slave_addr, TCPC_REG_ALERT,
				TCPC_REG_ALERT_VENDOR_DEF);
		if (rv != EC_SUCCESS) {
			CPRINTF("Fail to clear Vendor Define mask\n");
			return;
		}
		nct38xx_ioex_event_handler(ioex);
	}
}

const struct ioexpander_drv nct38xx_ioexpander_drv = {
	.init              = &nct38xx_ioex_init,
	.get_level         = &nct38xx_ioex_get_level,
	.set_level         = &nct38xx_ioex_set_level,
	.get_flags_by_mask = &nct38xx_ioex_get_flags,
	.set_flags_by_mask = &nct38xx_ioex_set_flags_by_mask,
	.enable_interrupt  = &nct38xx_ioex_enable_interrupt,
};
