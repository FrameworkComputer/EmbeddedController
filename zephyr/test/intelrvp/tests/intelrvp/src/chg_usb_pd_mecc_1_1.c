/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "driver/retimer/bb_retimer_public.h"
#include "ec_commands.h"
#include "fakes.h"
#include "gpio_signal.h"
#include "intelrvp.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc/tcpci.h"
#include "usbc/utils.h"
#include "usbc_ppc.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

/* USB-C ports */
enum usbc_port {
	USBC_PORT_C0 = 0,
	USBC_PORT_C1,
#if defined(HAS_TASK_PD_C2)
	USBC_PORT_C2,
	USBC_PORT_C3,
#endif
	USBC_PORT_COUNT
};

#define device_dt(gpio_name)                                              \
	{                                                                 \
		.port = DEVICE_DT_GET(                                    \
			DT_GPIO_CTLR(DT_NODELABEL(gpio_name), gpios)),    \
		.pin = DT_GPIO_PIN(DT_NODELABEL(gpio_name), gpios),       \
		.dt_flags = 0xFF &                                        \
			    DT_GPIO_FLAGS(DT_NODELABEL(gpio_name), gpios) \
	}

/* TCPC AIC GPIO devices */
const struct gpio_dt_spec tcpc_aic_gpios_device[] = {
	[USBC_PORT_C0] = device_dt(usbc_tcpc_alrt_p0),
	[USBC_PORT_C1] = device_dt(usbc_tcpc_alrt_p0),
#if defined(HAS_TASK_PD_C2)
	[USBC_PORT_C2] = device_dt(usbc_tcpc_alrt_p2),
	[USBC_PORT_C3] = device_dt(usbc_tcpc_alrt_p3),
#endif
};

/* TCPC AIC GPIO Configuration */
const struct mecc_1_1_tcpc_aic_gpio_config_t mecc_1_1_tcpc_aic_gpios[] = {
	[USBC_PORT_C0] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p0)),
	},
	[USBC_PORT_C1] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p0)),
	},
#if defined(HAS_TASK_PD_C2)
	[USBC_PORT_C2] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p2)),
	},
	[USBC_PORT_C3] = {
		.tcpc_alert = GPIO_SIGNAL(DT_NODELABEL(usbc_tcpc_alrt_p3)),
	},
#endif
};

FAKE_VALUE_FUNC(bool, board_port_has_ppc, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VOID_FUNC(board_connect_c0_sbu, enum gpio_signal);

static int set_cnk_ctrl_cnt;

static int set_snk_ctrl(int port, int enable)
{
	set_cnk_ctrl_cnt++;
	return 0;
}

/* Helper function to reset all fakes */
static void reset_test_fakes(void)
{
	RESET_FAKE(board_port_has_ppc);
	RESET_FAKE(ppc_vbus_sink_enable);
	set_cnk_ctrl_cnt = 0;
}

ZTEST_USER(mtlrvp_chg_usb_pd_mecc, test_tcpc_get_alert_status_bus_type_embedded)
{
	uint16_t expt_ret = 0;
	/* Test when tcpc bus type for all ports is EC_BUS_TYPE_EMBEDDED */
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		tcpc_config[i].bus_type = EC_BUS_TYPE_EMBEDDED;

	uint16_t ret = tcpc_get_alert_status();

	zassert_equal(expt_ret, ret, "Value mismatch expt_ret:%u while ret:%u",
		      expt_ret, ret);
}

ZTEST_USER(mtlrvp_chg_usb_pd_mecc, test_tcpc_get_alert_status_gpio_all_notset)
{
	/* Test when tcpc bus type for all ports is not EC_BUS_TYPE_EMBEDDED */
	uint16_t expt_ret = 0;
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		tcpc_config[i].bus_type = !EC_BUS_TYPE_EMBEDDED;
		gpio_pin_configure(tcpc_aic_gpios_device[i].port,
				   tcpc_aic_gpios_device[i].pin,
				   (GPIO_INPUT | GPIO_ACTIVE_LOW));
		gpio_emul_input_set(tcpc_aic_gpios_device[i].port,
				    tcpc_aic_gpios_device[i].pin, 0);
		expt_ret |= PD_STATUS_TCPC_ALERT_0 << i;
	}
	uint16_t ret = tcpc_get_alert_status();

	zassert_equal(expt_ret, ret, "Value mismatch expt_val:%u while ret:%u",
		      expt_ret, ret);
}

ZTEST_USER(mtlrvp_chg_usb_pd_mecc, test_tcpc_get_alert_status_gpio_all_set)
{
	uint16_t expt_ret = 0;
	for (int i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		tcpc_config[i].bus_type = !EC_BUS_TYPE_EMBEDDED;
		gpio_pin_configure(tcpc_aic_gpios_device[i].port,
				   tcpc_aic_gpios_device[i].pin,
				   (GPIO_INPUT | GPIO_ACTIVE_LOW));
		gpio_emul_input_set(tcpc_aic_gpios_device[i].port,
				    tcpc_aic_gpios_device[i].pin, 1);
	}

	uint16_t ret = tcpc_get_alert_status();

	zassert_equal(expt_ret, ret, "Value mismatch expt_val:%u while ret:%u",
		      expt_ret, ret);
}

ZTEST_USER(mtlrvp_chg_usb_pd_mecc, test_board_charging_enable_ppc_enable_0)
{
	board_port_has_ppc_fake.return_val = 1;
	ppc_vbus_sink_enable_fake.return_val = 1;

	/* Enable board charging for port 0 */
	board_charging_enable(0, 1);

	zassert_equal(1, ppc_vbus_sink_enable_fake.call_count,
		      "ppc_vbus_sink_enable mismatch");
}

ZTEST_USER(mtlrvp_chg_usb_pd_mecc, test_board_charging_enable_ppc_disable_0)
{
	board_port_has_ppc_fake.return_val = 0;

	const struct tcpm_drv tcpm_driver_mock = {
		.set_snk_ctrl = set_snk_ctrl,
	};

	/* Create mock tcpm driver and enable board charging for port 0 */
	tcpc_config[0].drv = &tcpm_driver_mock;
	board_charging_enable(0, 1);

	zassert_equal(1, set_cnk_ctrl_cnt, "set_snk_ctrl call count mismatch");
}

ZTEST_SUITE(mtlrvp_chg_usb_pd_mecc, NULL, NULL, reset_test_fakes, NULL, NULL);
