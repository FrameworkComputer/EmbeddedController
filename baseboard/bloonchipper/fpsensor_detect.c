/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fpsensor/fpsensor_detect.h"
#include "gpio.h"
#include "timer.h"

test_mockable enum fp_transport_type get_fp_transport_type(void)
{
	enum fp_transport_type ret;

	gpio_set_level(GPIO_DIVIDER_HIGHSIDE, 1);
	crec_usleep(1);
	switch (gpio_get_level(GPIO_TRANSPORT_SEL)) {
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
	gpio_set_level(GPIO_DIVIDER_HIGHSIDE, 0);
	return ret;
}
