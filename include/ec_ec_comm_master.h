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

#if defined(CONFIG_EC_EC_COMM_MASTER) && defined(CONFIG_EC_EC_COMM_BATTERY)
#define CONFIG_EC_EC_COMM_BATTERY_MASTER
#endif

/*
 * TODO(b:65697620): Move these to some other C file, depending on a config
 * option.
 */
extern struct ec_response_battery_static_info base_battery_static;
extern struct ec_response_battery_dynamic_info base_battery_dynamic;

/**
 * Sends EC_CMD_BATTERY_GET_DYNAMIC command to slave, and writes the
 * battery dynamic information into base_battery_dynamic.
 *
 * Leaves base_battery_dynamic intact on error: it is the callers responsability
 * to clear the data or ignore it.

 * @return EC_RES_SUCCESS on success, EC_RES_ERROR on communication error,
 * else forwards the error code from the slave.
 */
int ec_ec_master_base_get_dynamic_info(void);

/**
 * Sends EC_CMD_BATTERY_GET_STATIC command to slave, and writes the
 * battery static information into base_static_dynamic.
 *
 * Leaves base_battery_static intact on error: it is the callers responsability
 * to clear the data or ignore it.
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

#endif /* EC_EC_COMM_MASTER_H_ */
