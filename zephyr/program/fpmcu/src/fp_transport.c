/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

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
