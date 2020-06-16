/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Regulator control module for Chrome EC */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "host_command.h"
#include "regulator.h"

static enum ec_status
hc_regulator_get_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_regulator_get_info *p = args->params;
	struct ec_response_regulator_get_info *r = args->response;
	int rv;

	rv = board_regulator_get_info(p->index, r->name, &r->num_voltages,
				      r->voltages_mv);

	if (rv)
		return EC_RES_ERROR;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_REGULATOR_GET_INFO, hc_regulator_get_info,
		     EC_VER_MASK(0));

static enum ec_status
hc_regulator_enable(struct host_cmd_handler_args *args)
{
	const struct ec_params_regulator_enable *p = args->params;
	int rv;

	rv = board_regulator_enable(p->index, p->enable);

	if (rv)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_REGULATOR_ENABLE, hc_regulator_enable,
		     EC_VER_MASK(0));

static enum ec_status
hc_regulator_is_enabled(struct host_cmd_handler_args *args)
{
	const struct ec_params_regulator_is_enabled *p = args->params;
	struct ec_response_regulator_is_enabled *r = args->response;
	int rv;

	rv = board_regulator_is_enabled(p->index, &r->enabled);

	if (rv)
		return EC_RES_ERROR;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_REGULATOR_IS_ENABLED, hc_regulator_is_enabled,
		     EC_VER_MASK(0));

static enum ec_status
hc_regulator_get_voltage(struct host_cmd_handler_args *args)
{
	const struct ec_params_regulator_get_voltage *p = args->params;
	struct ec_response_regulator_get_voltage *r = args->response;
	int rv;

	rv = board_regulator_get_voltage(p->index, &r->voltage_mv);

	if (rv)
		return EC_RES_ERROR;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_REGULATOR_GET_VOLTAGE, hc_regulator_get_voltage,
		     EC_VER_MASK(0));

static enum ec_status
hc_regulator_set_voltage(struct host_cmd_handler_args *args)
{
	const struct ec_params_regulator_set_voltage *p = args->params;
	int rv;

	rv = board_regulator_set_voltage(p->index, p->min_mv, p->max_mv);

	if (rv)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_REGULATOR_SET_VOLTAGE, hc_regulator_set_voltage,
		     EC_VER_MASK(0));
