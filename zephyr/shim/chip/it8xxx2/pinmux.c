/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <init.h>
#include <drivers/pinmux.h>
#include <dt-bindings/pinctrl/it8xxx2-pinctrl.h>
#include <soc.h>

#define SCL 0
#define SDA 1
#define IT8XXX2_I2C_DEV_PINMUX(node, signal)    DEVICE_DT_GET(DT_PHANDLE( \
	DT_PINCTRL_BY_IDX(DT_NODELABEL(node), 0, signal), pinctrls))
#define IT8XXX2_I2C_DEV_PIN(node, signal)       DT_PHA( \
	DT_PINCTRL_BY_IDX(DT_NODELABEL(node), 0, signal), pinctrls, pin)
#define IT8XXX2_I2C_DEV_ALT_FUNC(node, signal)  DT_PHA( \
	DT_PINCTRL_BY_IDX(DT_NODELABEL(node), 0, signal), pinctrls, alt_func)

static int it8xxx2_pinmux_init(const struct device *dev)
{
	ARG_UNUSED(dev);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(pinmuxb), okay) && \
	DT_NODE_HAS_STATUS(DT_NODELABEL(uart1), okay)
	const struct device *portb = DEVICE_DT_GET(DT_NODELABEL(pinmuxb));

	/* SIN0 */
	pinmux_pin_set(portb, 0, IT8XXX2_PINMUX_FUNC_3);
	/* SOUT0 */
	pinmux_pin_set(portb, 1, IT8XXX2_PINMUX_FUNC_3);
#endif

	return 0;
}
SYS_INIT(it8xxx2_pinmux_init, PRE_KERNEL_1, CONFIG_PINMUX_INIT_PRIORITY);

/*
 * Init priority is behind CONFIG_PLATFORM_EC_GPIO_INIT_PRIORITY to overwrite
 * GPIO_INPUT setting of i2c ports.
 */
static int it8xxx2_pinmux_init_latr(const struct device *dev)
{
	ARG_UNUSED(dev);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c0), okay) && \
	DT_NODE_HAS_STATUS(DT_NODELABEL(pinmuxb), okay)
	{
		const struct device *portb =
				DEVICE_DT_GET(DT_NODELABEL(pinmuxb));

		/* I2C0 CLK */
		pinmux_pin_set(portb, 3, IT8XXX2_PINMUX_FUNC_1);
		/* I2C0 DAT */
		pinmux_pin_set(portb, 4, IT8XXX2_PINMUX_FUNC_1);
	}
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c1), okay) && \
	DT_NODE_HAS_STATUS(DT_NODELABEL(pinmuxc), okay)
	{
		const struct device *portc =
				DEVICE_DT_GET(DT_NODELABEL(pinmuxc));

		/* I2C1 CLK */
		pinmux_pin_set(portc, 1, IT8XXX2_PINMUX_FUNC_1);
		/* I2C1 DAT */
		pinmux_pin_set(portc, 2, IT8XXX2_PINMUX_FUNC_1);
	}
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c2), okay) && \
	DT_NODE_HAS_STATUS(DT_NODELABEL(pinmuxf), okay)
	{
		const struct device *portf =
				DEVICE_DT_GET(DT_NODELABEL(pinmuxf));

		/* I2C2 CLK */
		pinmux_pin_set(portf, 6, IT8XXX2_PINMUX_FUNC_1);
		/* I2C2 DAT */
		pinmux_pin_set(portf, 7, IT8XXX2_PINMUX_FUNC_1);
	}
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c3), okay)
	/* Pinmux control group */
	const struct device *clk_pinctrls = IT8XXX2_I2C_DEV_PINMUX(i2c3, SCL);
	const struct device *dat_pinctrls = IT8XXX2_I2C_DEV_PINMUX(i2c3, SDA);
	/* GPIO pin */
	uint8_t clk_pin = IT8XXX2_I2C_DEV_PIN(i2c3, SCL);
	uint8_t dat_pin = IT8XXX2_I2C_DEV_PIN(i2c3, SDA);
	/* Alternate function */
	uint8_t clk_alt = IT8XXX2_I2C_DEV_ALT_FUNC(i2c3, SCL);
	uint8_t dat_alt = IT8XXX2_I2C_DEV_ALT_FUNC(i2c3, SDA);

	/* I2C3 CLK */
	pinmux_pin_set(clk_pinctrls, clk_pin, clk_alt);
	/* I2C3 DAT */
	pinmux_pin_set(dat_pinctrls, dat_pin, dat_alt);
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c4), okay) && \
	DT_NODE_HAS_STATUS(DT_NODELABEL(pinmuxe), okay)
	{
		const struct device *porte =
				DEVICE_DT_GET(DT_NODELABEL(pinmuxe));

		/* I2C4 CLK */
		pinmux_pin_set(porte, 0, IT8XXX2_PINMUX_FUNC_3);
		/* I2C4 DAT */
		pinmux_pin_set(porte, 7, IT8XXX2_PINMUX_FUNC_3);
	}
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c5), okay) && \
	DT_NODE_HAS_STATUS(DT_NODELABEL(pinmuxa), okay)
	{
		const struct device *porta =
				DEVICE_DT_GET(DT_NODELABEL(pinmuxa));

		/* I2C5 CLK */
		pinmux_pin_set(porta, 4, IT8XXX2_PINMUX_FUNC_3);
		/* I2C5 DAT */
		pinmux_pin_set(porta, 5, IT8XXX2_PINMUX_FUNC_3);
	}
#endif

	return 0;
}
SYS_INIT(it8xxx2_pinmux_init_latr, POST_KERNEL, 52);
