/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * EC-EC communication, functions for client.
 */

#ifndef EC_EC_COMM_CLIENT_H_
#define EC_EC_COMM_CLIENT_H_

#include "config.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sends EC_CMD_BATTERY_GET_DYNAMIC command to server, and writes the
 * battery dynamic information into battery_dynamic[BATT_IDX_BASE].
 *
 * Leaves battery_dynamic[BATT_IDX_BASE] intact on error: it is the callers
 * responsibility to clear the data or ignore it.

 * @return EC_RES_SUCCESS on success, EC_RES_ERROR on communication error,
 * else forwards the error code from the server.
 */
int ec_ec_client_base_get_dynamic_info(void);

/**
 * Sends EC_CMD_BATTERY_GET_STATIC command to server, and writes the
 * battery static information into battery_static[BATT_IDX_BASE].
 *
 * Leaves battery_static[BATT_IDX_BASE] intact on error: it is the callers
 * responsibility to clear the data or ignore it.
 *
 * @return EC_RES_SUCCESS on success, EC_RES_ERROR on communication error,
 * else forwards the error code from the server.
 */
int ec_ec_client_base_get_static_info(void);

/**
 * Sends EC_CMD_CHARGER_CONTROL command to server, with the given parameters
 * (see ec_commands.h/ec_params_charger_control for description).
 *
 * @return EC_RES_SUCCESS on success, EC_RES_ERROR on communication error,
 * else forwards the error code from the server.
 */
int ec_ec_client_base_charge_control(int max_current, int otg_voltage,
				     int allow_charging);

/**
 * Sends EC_CMD_REBOOT_EC command to server, with EC_REBOOT_HIBERNATE parameter.
 *
 * @return EC_RES_ERROR on communication error (should always be the case if the
 * server successfully hibernates, as it will not be able to write back the
 * response, else forwards the error code from the server.
 */
int ec_ec_client_hibernate(void);

#ifdef __cplusplus
}
#endif

#endif /* EC_EC_COMM_CLIENT_H_ */
