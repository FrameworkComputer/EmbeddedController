/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_H_
#define ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_H_

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#include <drivers/fingerprint.h>

struct egis660_cfg {
	struct spi_dt_spec spi;
	struct gpio_dt_spec interrupt;
	struct gpio_dt_spec reset_pin;
	struct fingerprint_sensor_info sensor_info;
	struct fingerprint_image_frame_params sensor_image_configs[];
};

struct egis660_data {
	const struct device *dev;
	fingerprint_callback_t callback;
	struct gpio_callback irq_cb;
	struct k_sem sensor_lock;
	k_tid_t sensor_owner;
	uint16_t errors;
};

#endif /* ZEPHYR_DRIVERS_FINGERPRINT_EGIS660_H_ */
