/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USB Power delivery port management For Cypress EZ-PD CCG6DF, CCG6SF
 * CCGXXF FW is designed to adapt standard TCPM driver procedures.
 */
#ifndef __CROS_EC_DRIVER_TCPM_CCGXXF_H
#define __CROS_EC_DRIVER_TCPM_CCGXXF_H

#define CCGXXF_I2C_ADDR1_FLAGS 0x0B
#define CCGXXF_I2C_ADDR2_FLAGS 0x1B

/* SBU FET control register */
#define CCGXXF_REG_SBU_MUX_CTL 0xBB

/* F/W info register */
#define CCGXXF_REG_FW_VERSION 0x94
#define CCGXXF_REG_FW_VERSION_BUILD 0x96

/* Firmware update / reset control register */
#define CCGXXF_REG_FWU_COMMAND 0x92
#define CCGXXF_FWU_CMD_RESET 0x0077

/**
 * Reset CCGXXF chip
 *
 * CCGXXF's reset line is connected to an internal LDO hence external GPIOs
 * should not control the reset line as it can prevent it booting from dead
 * battery, instead a software mechanism can be used to reset the chip.
 * Care must be taken by board level function in below scenarios;
 * 1. During dead battery boot from CCGXXF ports, do not reset the chip as
 *    it will lose the dead battery boot scenario content.
 * 2. If dual port solution chip is used, resetting one port resets other port
 *    as well.
 * 3. Built-in I/O expander also gets reset.
 *
 * @param port Type-C port number
 * @return EC_SUCCESS or error
 */
int ccgxxf_reset(int port);

extern const struct tcpm_drv ccgxxf_tcpm_drv;

/* CCGXXF built in I/O expander definitions */
#ifdef CONFIG_IO_EXPANDER_CCGXXF

/* CCGXXF I/O ports that can be referenced in gpio.inc */
enum ccgxxf_io_ports {
	CCGXXF_PORT_0,
	CCGXXF_PORT_1,
	CCGXXF_PORT_2,
	CCGXXF_PORT_3
};

/* CCGXXF I/O pins that can be referenced in gpio.inc */
enum ccgxxf_io_pins {
	CCGXXF_IO_0,
	CCGXXF_IO_1,
	CCGXXF_IO_2,
	CCGXXF_IO_3,
	CCGXXF_IO_4,
	CCGXXF_IO_5,
	CCGXXF_IO_6,
	CCGXXF_IO_7
};

#define CCGXXF_REG_GPIO_CONTROL(port) ((port) + 0x80)
#define CCGXXF_REG_GPIO_STATUS(port) ((port) + 0x84)

#define CCGXXF_REG_GPIO_MODE 0x88
#define CCGXXF_GPIO_PIN_MASK_SHIFT 8
#define CCGXXF_GPIO_PIN_MODE_SHIFT 2
#define CCGXXF_GPIO_1P8V_SEL BIT(7)

enum ccgxxf_gpio_mode {
	CCGXXF_GPIO_MODE_HIZ_ANALOG,
	CCGXXF_GPIO_MODE_HIZ_DIGITAL,
	CCGXXF_GPIO_MODE_RES_UP,
	CCGXXF_GPIO_MODE_RES_DWN,
	CCGXXF_GPIO_MODE_OD_LOW,
	CCGXXF_GPIO_MODE_OD_HIGH,
	CCGXXF_GPIO_MODE_STRONG,
	CCGXXF_GPIO_MODE_RES_UPDOWN
};

extern const struct ioexpander_drv ccgxxf_ioexpander_drv;

#endif /* CONFIG_IO_EXPANDER_CCGXXF */

#endif /* __CROS_EC_DRIVER_TCPM_CCGXXF_H */
