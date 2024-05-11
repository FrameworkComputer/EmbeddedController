/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_BC12_MT6360_PUBLIC_H
#define __CROS_EC_DRIVER_BC12_MT6360_PUBLIC_H

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MT6360_PMU_I2C_ADDR_FLAGS 0x34
#define MT6360_PMIC_I2C_ADDR_FLAGS 0x1a
#define MT6360_LDO_I2C_ADDR_FLAGS 0x64
#define MT6360_PD_I2C_ADDR_FLAGS 0x4e

enum mt6360_regulator_id {
	MT6360_LDO3,
	MT6360_LDO5,
	MT6360_LDO6,
	MT6360_LDO7,
	MT6360_BUCK1,
	MT6360_BUCK2,

	MT6360_REGULATOR_COUNT,
};

int mt6360_regulator_get_info(enum mt6360_regulator_id id, char *name,
			      uint16_t *voltage_count, uint16_t *voltages_mv);

int mt6360_regulator_enable(enum mt6360_regulator_id id, uint8_t enable);

int mt6360_regulator_is_enabled(enum mt6360_regulator_id id, uint8_t *enabled);

int mt6360_regulator_set_voltage(enum mt6360_regulator_id id, int min_mv,
				 int max_mv);

int mt6360_regulator_get_voltage(enum mt6360_regulator_id id, int *voltage_mv);

enum mt6360_led_id {
	MT6360_LED_RGB1,
	MT6360_LED_RGB2,
	MT6360_LED_RGB3,
	MT6360_LED_RGB_ML,

	MT6360_LED_COUNT,
};

#define MT6360_LED_BRIGHTNESS_MAX 15

int mt6360_led_enable(enum mt6360_led_id led_id, int enable);

int mt6360_led_set_brightness(enum mt6360_led_id led_id, int brightness);

extern const struct mt6360_config_t mt6360_config;

struct mt6360_config_t {
	int i2c_port;
	int i2c_addr_flags;
};
extern const struct bc12_drv mt6360_drv;

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_BC12_MT6360_PUBLIC_H */
