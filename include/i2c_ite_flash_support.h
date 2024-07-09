/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* API for module that provides flash support for ITE-based ECs over i2c */

#ifndef __CROS_EC_I2C_ITE_FLASH_SUPPORT_H
#define __CROS_EC_I2C_ITE_FLASH_SUPPORT_H

#include "gpio.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ite_dfu_config_t {
	/* I2C port to communicate on */
	int i2c_port;
	/* True if using OC1N instead of OC1 */
	bool use_complement_timer_channel;
	/*
	 * Optional function that guards access to i2c port. If present, the
	 * return value should return true if dfu access is allowed and false
	 * otherwise.
	 */
	bool (*access_allow)(void);
	/*
	 * The gpio signals that moved between TIM16/17 (MODULE_I2C_TIMERS) and
	 * I2C (MODULE_I2C).
	 */
	enum gpio_signal scl;
	enum gpio_signal sda;
};

/* Provided by board implementation if CONFIG_ITE_FLASH_SUPPORT is used */
const extern struct ite_dfu_config_t ite_dfu_config;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_I2C_ITE_FLASH_SUPPORT_H */
