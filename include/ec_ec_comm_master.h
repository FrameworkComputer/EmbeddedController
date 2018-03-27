/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * EC-EC communication, functions for master.
 */

#ifndef EC_EC_COMM_MASTER_H_
#define EC_EC_COMM_MASTER_H_

#include <stdint.h>
#include "config.h"

/**
 * Sends EC_CMD_BATTERY_GET_DYNAMIC command to slave, and writes the
 * battery dynamic information into battery_dynamic[BATT_IDX_BASE].
 *
 * Leaves battery_dynamic[BATT_IDX_BASE] intact on error: it is the callers
 * responsibility to clear the data or ignore it.

 * @return EC_RES_SUCCESS on success, EC_RES_ERROR on communication error,
 * else forwards the error code from the slave.
 */
int ec_ec_master_base_get_dynamic_info(void);

/**
 * Sends EC_CMD_BATTERY_GET_STATIC command to slave, and writes the
 * battery static information into battery_static[BATT_IDX_BASE].
 *
 * Leaves battery_static[BATT_IDX_BASE] intact on error: it is the callers
 * responsibility to clear the data or ignore it.
 *
 * @return EC_RES_SUCCESS on success, EC_RES_ERROR on communication error,
 * else forwards the error code from the slave.
 */
int ec_ec_master_base_get_static_info(void);

/**
 * Sends EC_CMD_CHARGER_CONTROL command to slave, with the given parameters
 * (see ec_commands.h/ec_params_charger_control for description).
 *
 * @return EC_RES_SUCCESS on success, EC_RES_ERROR on communication error,
 * else forwards the error code from the slave.
 */
int ec_ec_master_base_charge_control(int max_current,
				     int otg_voltage,
				     int allow_charging);

/**
 * Sends EC_CMD_REBOOT_EC command to slave, with EC_REBOOT_HIBERNATE parameter.
 *
 * @return EC_RES_ERROR on communication error (should always be the case if the
 * slave successfully hibernates, as it will not be able to write back the
 * response, else forwards the error code from the slave.
 */
int ec_ec_master_hibernate(void);

#endif /* EC_EC_COMM_MASTER_H_ */
