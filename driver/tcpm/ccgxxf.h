/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * USB Power delivery port management For Cypress EZ-PD CCG6DF, CCG6SF
 * CCGXXF FW is designed to adapt standard TCPM driver procedures.
 */
#ifndef __CROS_EC_DRIVER_TCPM_CCGXXF_H
#define __CROS_EC_DRIVER_TCPM_CCGXXF_H

#define CCGXXF_I2C_ADDR1_FLAGS	0x0B
#define CCGXXF_I2C_ADDR2_FLAGS	0x1B

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

#define CCGXXF_REG_GPIO_CONTROL(port)	((port) + 0x80)
#define CCGXXF_REG_GPIO_STATUS(port)	((port) + 0x84)

#define CCGXXF_REG_GPIO_MODE		0x88
#define CCGXXF_GPIO_PIN_MASK_SHIFT	8
#define CCGXXF_GPIO_PIN_MODE_SHIFT	2
#define CCGXXF_GPIO_1P8V_SEL		BIT(7)

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
