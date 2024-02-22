/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/retimer/anx7452.h"
#include "driver/retimer/anx7452_public.h"
#include "emul/emul_anx7452.h"
#include "emul/emul_common_i2c.h"
#include "gpio.h"
#include "i2c.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"
#include "usb_mux.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define GPIO_USB_C1_USB_EN_PATH NAMED_GPIOS_GPIO_NODE(usb_c1_usb_en)
#define GPIO_USB_C1_USB_EN_PORT DT_GPIO_PIN(GPIO_USB_C1_USB_EN_PATH, gpios)
#define GPIO_USB_C1_USB_EN_SIGNAL GPIO_SIGNAL(GPIO_USB_C1_USB_EN_PATH)
#define GPIO_USB_C1_DP_EN_PATH NAMED_GPIOS_GPIO_NODE(usb_c1_dp_en)
#define GPIO_USB_C1_DP_EN_PORT DT_GPIO_PIN(GPIO_USB_C1_DP_EN_PATH, gpios)

#define ANX7452_NODE DT_NODELABEL(usb_c1_anx7452_emul)
#define EMUL EMUL_DT_GET(ANX7452_NODE)
#define COMMON_DATA(port) emul_anx7452_get_i2c_common_data(EMUL, port)

static inline void reset_anx7452_state(void)
{
	test_set_chipset_to_s0();
	i2c_common_emul_set_write_func(COMMON_DATA(TOP_EMUL_PORT), NULL, NULL);
	i2c_common_emul_set_read_func(COMMON_DATA(TOP_EMUL_PORT), NULL, NULL);
	i2c_common_emul_set_write_fail_reg(COMMON_DATA(TOP_EMUL_PORT),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_fail_reg(COMMON_DATA(TOP_EMUL_PORT),
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	i2c_common_emul_set_write_func(COMMON_DATA(CTLTOP_EMUL_PORT), NULL,
				       NULL);
	i2c_common_emul_set_read_func(COMMON_DATA(CTLTOP_EMUL_PORT), NULL,
				      NULL);
	i2c_common_emul_set_write_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	gpio_set_level(GPIO_USB_C1_USB_EN_SIGNAL, 0);

	anx7452_emul_reset(EMUL);
}

ZTEST(anx7452, test_anx7452_init)
{
	const struct device *usb_en_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_USB_C1_USB_EN_PATH, gpios));
	const struct device *dp_en_gpio_dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(GPIO_USB_C1_DP_EN_PATH, gpios));
	const uint8_t top_reg_val = ANX7452_TOP_REG_EN |
				    ANX7452_TOP_RESERVED_BIT |
				    ANX7452_TOP_USB3_INFO;
	uint32_t start_ms;
	uint32_t end_ms;

	zassert_not_null(usb_en_gpio_dev, "Cannot get USB EN GPIO device");
	zassert_not_null(dp_en_gpio_dev, "Cannot get DP EN GPIO device");

	/* Test successful init */
	zassert_equal(
		EC_SUCCESS,
		anx7452_usb_retimer_driver.init(usb_muxes[USBC_PORT_C1].mux),
		NULL);
	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_TOP_STATUS_REG),
		      top_reg_val, NULL);
	zassert_equal(1,
		      gpio_emul_output_get(usb_en_gpio_dev,
					   GPIO_USB_C1_USB_EN_PORT),
		      NULL);
	zassert_equal(
		0, gpio_emul_output_get(dp_en_gpio_dev, GPIO_USB_C1_DP_EN_PORT),
		NULL);

	reset_anx7452_state();
	/* Setup emulator fail on write */
	i2c_common_emul_set_write_fail_reg(COMMON_DATA(TOP_EMUL_PORT),
					   ANX7452_TOP_STATUS_REG);
	/* With reg write fail, init should fail */
	zassert_equal(
		EC_ERROR_TIMEOUT,
		anx7452_usb_retimer_driver.init(usb_muxes[USBC_PORT_C1].mux),
		NULL);
	zassert_not_equal(anx7452_emul_get_reg(EMUL, ANX7452_TOP_STATUS_REG),
			  top_reg_val, NULL);
	zassert_equal(1,
		      gpio_emul_output_get(usb_en_gpio_dev,
					   GPIO_USB_C1_USB_EN_PORT),
		      NULL);
	zassert_equal(
		0, gpio_emul_output_get(dp_en_gpio_dev, GPIO_USB_C1_DP_EN_PORT),
		NULL);

	reset_anx7452_state();
	/* Setup emulator fail on read */
	i2c_common_emul_set_read_fail_reg(COMMON_DATA(TOP_EMUL_PORT),
					  ANX7452_TOP_STATUS_REG);
	start_ms = k_uptime_get();
	/* With reg read fail, init should fail */
	zassert_equal(
		EC_ERROR_TIMEOUT,
		anx7452_usb_retimer_driver.init(usb_muxes[USBC_PORT_C1].mux),
		NULL);
	end_ms = k_uptime_get();
	/* With timeout caused by read fail, the time took should be greater
	 * than or equal to configured timeout value
	 */
	zassert_true((end_ms - start_ms) > ANX7452_I2C_WAKE_TIMEOUT_MS, NULL);
	zassert_not_equal(anx7452_emul_get_reg(EMUL, ANX7452_TOP_STATUS_REG),
			  top_reg_val, NULL);
	zassert_equal(1,
		      gpio_emul_output_get(usb_en_gpio_dev,
					   GPIO_USB_C1_USB_EN_PORT),
		      NULL);
	zassert_equal(
		0, gpio_emul_output_get(dp_en_gpio_dev, GPIO_USB_C1_DP_EN_PORT),
		NULL);

	reset_anx7452_state();
	/* Set AP to off state and wait for chipset task */
	test_set_chipset_to_g3();
	start_ms = k_uptime_get();
	/* With AP off, init should fail and pins should be unset */
	zassert_equal(
		EC_ERROR_NOT_POWERED,
		anx7452_usb_retimer_driver.init(usb_muxes[USBC_PORT_C1].mux),
		NULL);
	end_ms = k_uptime_get();
	zassert_true((end_ms - start_ms) < 30, NULL);
	zassert_not_equal(anx7452_emul_get_reg(EMUL, ANX7452_TOP_STATUS_REG),
			  top_reg_val, NULL);
	zassert_equal(0,
		      gpio_emul_output_get(usb_en_gpio_dev,
					   GPIO_USB_C1_USB_EN_PORT),
		      NULL);
	zassert_equal(
		0, gpio_emul_output_get(dp_en_gpio_dev, GPIO_USB_C1_DP_EN_PORT),
		NULL);
}

ZTEST(anx7452, test_anx7452_get)
{
	mux_state_t mux_state = 0;

	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.get(
			      usb_muxes[USBC_PORT_C1].mux, &mux_state));

	anx7452_emul_set_reg(EMUL, ANX7452_TOP_STATUS_REG,
			     ANX7452_TOP_REG_EN | ANX7452_TOP_FLIP_INFO);
	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.get(
			      usb_muxes[USBC_PORT_C1].mux, &mux_state));
	zassert_equal(mux_state, USB_PD_MUX_POLARITY_INVERTED);

	anx7452_emul_set_reg(EMUL, ANX7452_TOP_STATUS_REG,
			     ANX7452_TOP_REG_EN | ANX7452_TOP_DP_INFO);
	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.get(
			      usb_muxes[USBC_PORT_C1].mux, &mux_state));
	zassert_equal(mux_state, USB_PD_MUX_DP_ENABLED);

	anx7452_emul_set_reg(EMUL, ANX7452_TOP_STATUS_REG,
			     ANX7452_TOP_REG_EN | ANX7452_TOP_TBT_INFO);
	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.get(
			      usb_muxes[USBC_PORT_C1].mux, &mux_state));
	zassert_equal(mux_state, USB_PD_MUX_TBT_COMPAT_ENABLED);

	anx7452_emul_set_reg(EMUL, ANX7452_TOP_STATUS_REG,
			     ANX7452_TOP_REG_EN | ANX7452_TOP_USB3_INFO);
	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.get(
			      usb_muxes[USBC_PORT_C1].mux, &mux_state));
	zassert_equal(mux_state, USB_PD_MUX_USB_ENABLED);

	anx7452_emul_set_reg(EMUL, ANX7452_TOP_STATUS_REG,
			     ANX7452_TOP_REG_EN | ANX7452_TOP_USB4_INFO);
	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.get(
			      usb_muxes[USBC_PORT_C1].mux, &mux_state));
	zassert_equal(mux_state, USB_PD_MUX_USB4_ENABLED);

	reset_anx7452_state();
	test_set_chipset_to_g3();
	zassert_equal(EC_ERROR_NOT_POWERED,
		      anx7452_usb_retimer_driver.get(
			      usb_muxes[USBC_PORT_C1].mux, &mux_state));
	zassert_equal(mux_state, USB_PD_MUX_NONE);

	reset_anx7452_state();
	i2c_common_emul_set_read_fail_reg(COMMON_DATA(TOP_EMUL_PORT),
					  ANX7452_TOP_STATUS_REG);
	zassert_equal(EC_ERROR_INVAL,
		      anx7452_usb_retimer_driver.get(
			      usb_muxes[USBC_PORT_C1].mux, &mux_state));
}

ZTEST(anx7452, test_anx7452_set)
{
	mux_state_t mux_state = 0;
	bool ack_required;

	zassert_equal(EC_SUCCESS, anx7452_usb_retimer_driver.set(
					  usb_muxes[USBC_PORT_C1].mux,
					  mux_state, &ack_required));
	zassert_equal(ack_required, false);

	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_CTLTOP_CFG0_REG), 0);
	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.set(
			      usb_muxes[USBC_PORT_C1].mux,
			      USB_PD_MUX_POLARITY_INVERTED, &ack_required));
	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_CTLTOP_CFG0_REG),
		      ANX7452_CTLTOP_CFG0_FLIP_EN);

	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.set(
			      usb_muxes[USBC_PORT_C1].mux,
			      USB_PD_MUX_USB_ENABLED, &ack_required));
	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_CTLTOP_CFG0_REG),
		      ANX7452_CTLTOP_CFG0_USB3_EN);

	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_CTLTOP_CFG1_REG), 0);
	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.set(
			      usb_muxes[USBC_PORT_C1].mux,
			      USB_PD_MUX_DP_ENABLED, &ack_required));
	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_CTLTOP_CFG1_REG),
		      ANX7452_CTLTOP_CFG1_DP_EN);

	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_CTLTOP_CFG2_REG), 0);
	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.set(
			      usb_muxes[USBC_PORT_C1].mux,
			      USB_PD_MUX_USB4_ENABLED, &ack_required));
	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_CTLTOP_CFG2_REG),
		      ANX7452_CTLTOP_CFG2_USB4_EN);

	zassert_equal(EC_SUCCESS,
		      anx7452_usb_retimer_driver.set(
			      usb_muxes[USBC_PORT_C1].mux,
			      USB_PD_MUX_TBT_COMPAT_ENABLED, &ack_required));
	zassert_equal(anx7452_emul_get_reg(EMUL, ANX7452_CTLTOP_CFG2_REG),
		      ANX7452_CTLTOP_CFG2_TBT_EN);

	i2c_common_emul_set_read_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					  ANX7452_CTLTOP_CFG0_REG);
	zassert_equal(EC_ERROR_INVAL, anx7452_usb_retimer_driver.set(
					      usb_muxes[USBC_PORT_C1].mux,
					      mux_state, &ack_required));

	i2c_common_emul_set_read_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					  ANX7452_CTLTOP_CFG1_REG);
	zassert_equal(EC_ERROR_INVAL, anx7452_usb_retimer_driver.set(
					      usb_muxes[USBC_PORT_C1].mux,
					      mux_state, &ack_required));

	i2c_common_emul_set_read_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_read_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					  ANX7452_CTLTOP_CFG2_REG);
	zassert_equal(EC_ERROR_INVAL, anx7452_usb_retimer_driver.set(
					      usb_muxes[USBC_PORT_C1].mux,
					      mux_state, &ack_required));

	i2c_common_emul_set_read_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					   ANX7452_CTLTOP_CFG0_REG);
	zassert_equal(EC_ERROR_INVAL,
		      anx7452_usb_retimer_driver.set(
			      usb_muxes[USBC_PORT_C1].mux,
			      USB_PD_MUX_POLARITY_INVERTED, &ack_required));

	i2c_common_emul_set_write_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					   ANX7452_CTLTOP_CFG1_REG);
	zassert_equal(EC_ERROR_INVAL,
		      anx7452_usb_retimer_driver.set(
			      usb_muxes[USBC_PORT_C1].mux,
			      USB_PD_MUX_DP_ENABLED, &ack_required));

	i2c_common_emul_set_write_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(COMMON_DATA(CTLTOP_EMUL_PORT),
					   ANX7452_CTLTOP_CFG2_REG);
	zassert_equal(EC_ERROR_INVAL,
		      anx7452_usb_retimer_driver.set(
			      usb_muxes[USBC_PORT_C1].mux,
			      USB_PD_MUX_USB4_ENABLED, &ack_required));

	reset_anx7452_state();
	test_set_chipset_to_g3();
	zassert_equal(EC_ERROR_NOT_POWERED, anx7452_usb_retimer_driver.set(
						    usb_muxes[USBC_PORT_C1].mux,
						    mux_state, &ack_required));
}

static void anx7452_before(void *state)
{
	ARG_UNUSED(state);
	reset_anx7452_state();
}

static void anx7452_after(void *state)
{
	ARG_UNUSED(state);
	reset_anx7452_state();
}

ZTEST_SUITE(anx7452, drivers_predicate_post_main, NULL, anx7452_before,
	    anx7452_after, NULL);
