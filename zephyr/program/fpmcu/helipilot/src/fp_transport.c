/* Copyright 2025 The ChromiumOS Authors
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

#if !defined(CONFIG_EC_HOST_CMD_BACKEND_SHI) && \
	!defined(CONFIG_EC_HOST_CMD_BACKEND_UART)
BUILD_ASSERT(0, "Both backends are not enabled");
#endif

#ifndef CONFIG_EC_HOST_CMD_INITIALIZE_AT_BOOT
test_export_static int fp_transport_init(void)
{
#ifdef CONFIG_EC_HOST_CMD_BACKEND_UART
	const struct device *const dev_uart =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_host_cmd_uart_backend));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_UART */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_SHI
	const struct device *const dev_shi =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_host_cmd_shi_backend));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_SPI */

	switch (get_fp_transport_type()) {
	case FP_TRANSPORT_TYPE_UART:
#ifdef CONFIG_EC_HOST_CMD_BACKEND_SHI
		pm_device_action_run(dev_shi, PM_DEVICE_ACTION_SUSPEND);
#endif /* CONFIG_EC_HOST_CMD_BACKEND_SHI */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_UART
		ec_host_cmd_init(ec_host_cmd_backend_get_uart(dev_uart));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_UART */

		break;
	case FP_TRANSPORT_TYPE_SPI:
#ifdef CONFIG_EC_HOST_CMD_BACKEND_UART
		pm_device_action_run(dev_uart, PM_DEVICE_ACTION_SUSPEND);
#endif /* CONFIG_EC_HOST_CMD_BACKEND_UART */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_SHI
		ec_host_cmd_init(ec_host_cmd_backend_get_shi_npcx());
#endif /* CONFIG_EC_HOST_CMD_BACKEND_SHI */

		break;
	default:
		break;
	}

	return 0;
}
SYS_INIT(fp_transport_init, POST_KERNEL, CONFIG_EC_HOST_CMD_INIT_PRIORITY);
#endif
