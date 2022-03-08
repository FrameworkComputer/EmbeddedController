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
#define IT8XXX2_DT_I2C_ALT_ITEMS(node_id) {                              \
	.clk_pinctrls = DEVICE_DT_GET(DT_PHANDLE(                        \
		DT_PINCTRL_BY_IDX(node_id, 0, SCL), pinctrls)),          \
	.dat_pinctrls = DEVICE_DT_GET(DT_PHANDLE(                        \
		DT_PINCTRL_BY_IDX(node_id, 0, SDA), pinctrls)),          \
	.clk_pin = DT_PHA(                                               \
		DT_PINCTRL_BY_IDX(node_id, 0, SCL), pinctrls, pin),      \
	.dat_pin = DT_PHA(                                               \
		DT_PINCTRL_BY_IDX(node_id, 0, SDA), pinctrls, pin),      \
	.clk_alt = DT_PHA(                                               \
		DT_PINCTRL_BY_IDX(node_id, 0, SCL), pinctrls, alt_func), \
	.dat_alt = DT_PHA(                                               \
		DT_PINCTRL_BY_IDX(node_id, 0, SDA), pinctrls, alt_func), \
},

struct i2c_alts_cfg {
	/* Pinmux control group */
	const struct device *clk_pinctrls;
	const struct device *dat_pinctrls;
	/* GPIO pin */
	uint8_t clk_pin;
	uint8_t dat_pin;
	/* Alternate function */
	uint8_t clk_alt;
	uint8_t dat_alt;
};

static struct i2c_alts_cfg i2c_alts[] = {
	DT_FOREACH_STATUS_OKAY(ite_it8xxx2_i2c, IT8XXX2_DT_I2C_ALT_ITEMS)
};

#ifdef CONFIG_I2C_ITE_ENHANCE
static struct i2c_alts_cfg i2c_alts_enhance[] = {
	DT_FOREACH_STATUS_OKAY(ite_enhance_i2c, IT8XXX2_DT_I2C_ALT_ITEMS)
};
#endif

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

	for (int inst = 0; inst < ARRAY_SIZE(i2c_alts); inst++) {
		/* I2C CLK */
		pinmux_pin_set(i2c_alts[inst].clk_pinctrls,
			       i2c_alts[inst].clk_pin,
			       i2c_alts[inst].clk_alt);
		/* I2C DAT */
		pinmux_pin_set(i2c_alts[inst].dat_pinctrls,
			       i2c_alts[inst].dat_pin,
			       i2c_alts[inst].dat_alt);
	}

#ifdef CONFIG_I2C_ITE_ENHANCE
	for (int inst = 0; inst < ARRAY_SIZE(i2c_alts_enhance); inst++) {
		/* I2C CLK */
		pinmux_pin_set(i2c_alts_enhance[inst].clk_pinctrls,
			       i2c_alts_enhance[inst].clk_pin,
			       i2c_alts_enhance[inst].clk_alt);
		/* I2C DAT */
		pinmux_pin_set(i2c_alts_enhance[inst].dat_pinctrls,
			       i2c_alts_enhance[inst].dat_pin,
			       i2c_alts_enhance[inst].dat_alt);
	}
#endif
	return 0;
}
SYS_INIT(it8xxx2_pinmux_init_latr, POST_KERNEL, 52);
