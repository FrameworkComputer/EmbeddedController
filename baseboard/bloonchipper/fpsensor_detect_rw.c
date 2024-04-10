/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fpsensor/fpsensor_detect.h"
#include "gpio.h"
#include "timer.h"

test_mockable enum fp_sensor_type fpsensor_detect_get_type(void)
{
	enum fp_sensor_type ret;

	gpio_set_level(GPIO_DIVIDER_HIGHSIDE, 1);
	crec_usleep(1);
	switch (gpio_get_level(GPIO_FP_SENSOR_SEL)) {
	case 0:
		ret = FP_SENSOR_TYPE_ELAN;
		break;
	case 1:
		ret = FP_SENSOR_TYPE_FPC;
		break;
	default:
		ret = FP_SENSOR_TYPE_UNKNOWN;
		break;
	}
	/* We leave GPIO_DIVIDER_HIGHSIDE enabled, since the dragonclaw
	 * development board use it to enable the AND gate (U10) to CS.
	 * Production boards could disable this to save power since it's
	 * only needed for initial detection on those boards.
	 */
	return ret;
}

test_mockable enum fp_sensor_spi_select fpsensor_detect_get_spi_select(void)
{
	return FP_SENSOR_SPI_SELECT_PRODUCTION;
}
