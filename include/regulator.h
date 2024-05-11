/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_REGULATOR_H
#define __CROS_EC_REGULATOR_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Board dependent hooks on voltage regulators.
 *
 * These functions should be implemented by boards which
 * CONFIG_HOSTCMD_REGULATOR is defined.
 */

/*
 * Get basic info of voltage regulator for given index.
 *
 * Note that the maximum length of name is EC_REGULATOR_NAME_MAX_LEN, and the
 * maximum length of the voltages_mv list is EC_REGULATOR_VOLTAGE_MAX_COUNT.
 */
int board_regulator_get_info(uint32_t index, char *name,
			     uint16_t *voltage_count, uint16_t *voltages_mv);

/*
 * Configure the regulator as enabled / disabled.
 */
int board_regulator_enable(uint32_t index, uint8_t enable);

/*
 * Query if the regulator is enabled.
 */
int board_regulator_is_enabled(uint32_t index, uint8_t *enabled);

/*
 * Set voltage for the voltage regulator within the range specified.
 *
 * The driver should select the voltage in range closest to min_mv.
 *
 * Also note that this might be called before the regulator is enabled, and the
 * setting should be in effect after the regulator is enabled.
 */
int board_regulator_set_voltage(uint32_t index, uint32_t min_mv,
				uint32_t max_mv);

/*
 * Get the currently configured voltage for the voltage regulator.
 *
 * Note that this might be called before the regulator is enabled.
 */
int board_regulator_get_voltage(uint32_t index, uint32_t *voltage_mv);

#ifdef __cplusplus
}
#endif

#endif /* !defined(__CROS_EC_REGULATOR_H) */
