/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_detect.h"
#include "gpio.h"
#include "timer.h"

enum fp_sensor_type get_fp_sensor_type(void)
{
	return FP_SENSOR_TYPE_FPC;
}

enum fp_sensor_spi_select get_fp_sensor_spi_select(void)
{
	enum fp_sensor_spi_select ret;

	gpio_set_level(GPIO_DIVIDER_HIGHSIDE, 1);
	usleep(1);
	switch (gpio_get_level(GPIO_FP_SPI_SEL)) {
	case 0:
		ret = FP_SENSOR_SPI_SELECT_DEVELOPMENT;
		break;
	case 1:
		ret = FP_SENSOR_SPI_SELECT_PRODUCTION;
		break;
	default:
		ret = FP_SENSOR_SPI_SELECT_UNKNOWN;
		break;
	}
	gpio_set_level(GPIO_DIVIDER_HIGHSIDE, 0);
	return ret;
}
