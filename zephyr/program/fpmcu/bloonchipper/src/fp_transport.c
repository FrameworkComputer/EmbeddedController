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

#include <ec_commands.h>
#include <fpsensor_detect.h>
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

/**
 * Get protocol information
 */
test_export_static enum ec_host_cmd_status
host_command_protocol_info(struct ec_host_cmd_handler_args *args)
{
	struct ec_response_get_protocol_info *r = args->output_buf;
	const struct ec_host_cmd *hc = ec_host_cmd_get_hc();

	r->protocol_versions = BIT(3);
	r->flags = EC_PROTOCOL_INFO_IN_PROGRESS_SUPPORTED;

	if (get_fp_transport_type() != FP_TRANSPORT_TYPE_UNKNOWN) {
		r->max_request_packet_size = hc->rx_ctx.len_max;
		r->max_response_packet_size = hc->tx.len_max;
	} else {
		r->max_request_packet_size = 0;
		r->max_response_packet_size = 0;
	}

	args->output_buf_size = sizeof(*r);

	return EC_HOST_CMD_SUCCESS;
}
EC_HOST_CMD_HANDLER_UNBOUND(EC_CMD_GET_PROTOCOL_INFO,
			    host_command_protocol_info, EC_VER_MASK(0));

test_export_static int fp_transport_init(void)
{
#ifdef CONFIG_EC_HOST_CMD_BACKEND_UART
	const struct device *const dev_uart =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_host_cmd_uart_backend));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_UART */
#ifdef CONFIG_EC_HOST_CMD_BACKEND_SPI
	struct gpio_dt_spec cs = GPIO_DT_SPEC_GET(
		DT_CHOSEN(zephyr_host_cmd_spi_backend), cs_gpios);
#endif /* CONFIG_EC_HOST_CMD_BACKEND_SPI */

	switch (get_fp_transport_type()) {
	case FP_TRANSPORT_TYPE_UART:
#ifdef CONFIG_EC_HOST_CMD_BACKEND_UART
		ec_host_cmd_init(ec_host_cmd_backend_get_uart(dev_uart));
#endif /* CONFIG_EC_HOST_CMD_BACKEND_UART */

		break;
	case FP_TRANSPORT_TYPE_SPI:
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
