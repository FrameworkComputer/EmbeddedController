/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/mgmt/ec_host_cmd/ec_host_cmd.h>
#include <zephyr/pm/device.h>

#include <ec_commands.h>
#include <fpsensor/fpsensor_detect.h>
#include <gpio_signal.h>

enum fp_transport_type get_fp_transport_type(void)
{
	static enum fp_transport_type ret = FP_TRANSPORT_TYPE_UNKNOWN;

	if (ret == FP_TRANSPORT_TYPE_UNKNOWN) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(div_highside), 1);
		k_usleep(1);
		switch (gpio_pin_get_dt(
			GPIO_DT_FROM_NODELABEL(transport_sel))) {
		case 0:
			ret = FP_TRANSPORT_TYPE_UART;
			break;
		case 1:
			ret = FP_TRANSPORT_TYPE_SPI;
			break;
		default:
			ret = FP_TRANSPORT_TYPE_UNKNOWN;
			break;
		}
	}

	return ret;
}

#if !defined(CONFIG_EC_HOST_CMD_BACKEND_SPI) && \
	!defined(CONFIG_EC_HOST_CMD_BACKEND_UART)
BUILD_ASSERT(0, "Both backends are not enabled");
#endif

test_export_static int fp_transport_init(void)
{
#ifdef CONFIG_EC_HOST_CMD_BACKEND_UART
	const struct device *const dev_uart =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_host_cmd_uart_backend));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_UART */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_SPI
	struct gpio_dt_spec cs = GPIO_DT_SPEC_GET(
		DT_CHOSEN(zephyr_host_cmd_spi_backend), cs_gpios);
	const struct device *const dev_spi =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_host_cmd_spi_backend));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_SPI */

	switch (get_fp_transport_type()) {
	case FP_TRANSPORT_TYPE_UART:
#ifdef CONFIG_EC_HOST_CMD_BACKEND_SPI
		if (!pm_device_action_run(dev_spi, PM_DEVICE_ACTION_SUSPEND)) {
			pm_device_state_lock(dev_spi);
		}
#endif /* CONFIG_EC_HOST_CMD_BACKEND_SPI */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_UART
		ec_host_cmd_init(ec_host_cmd_backend_get_uart(dev_uart));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_UART */

		break;
	case FP_TRANSPORT_TYPE_SPI:
#ifdef CONFIG_EC_HOST_CMD_BACKEND_UART
		if (!pm_device_action_run(dev_uart, PM_DEVICE_ACTION_SUSPEND)) {
			pm_device_state_lock(dev_uart);
		}
#endif /* CONFIG_EC_HOST_CMD_BACKEND_UART */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_SPI
		ec_host_cmd_init(ec_host_cmd_backend_get_spi(&cs));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_SPI */

		break;
	default:
		break;
	}

	return 0;
}
SYS_INIT(fp_transport_init, POST_KERNEL, CONFIG_EC_HOST_CMD_INIT_PRIORITY);
