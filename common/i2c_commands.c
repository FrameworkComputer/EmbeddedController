/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C host commands for Chrome EC */

#include "host_command.h"
#include "i2c.h"
#include "system.h"

int i2c_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_read *p =
		(const struct ec_params_i2c_read *)args->params;
	struct ec_response_i2c_read *r =
		(struct ec_response_i2c_read *)args->response;
	int data, rv = -1;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if  (p->read_size == 16)
		rv = i2c_read16(p->port, p->addr, p->offset, &data);
	else if (p->read_size == 8)
		rv = i2c_read8(p->port, p->addr, p->offset, &data);

	if (rv)
		return EC_RES_ERROR;
	r->data = data;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_READ, i2c_command_read, EC_VER_MASK(0));

int i2c_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_write *p =
		(const struct ec_params_i2c_write *)args->params;
	int rv = -1;

	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;

	if (p->write_size == 16)
		rv = i2c_write16(p->port, p->addr, p->offset, p->data);
	else if (p->write_size == 8)
		rv = i2c_write8(p->port, p->addr, p->offset, p->data);

	if (rv)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_WRITE, i2c_command_write, EC_VER_MASK(0));
