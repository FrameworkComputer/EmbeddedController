/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * The LTC4291 is a power over ethernet (PoE) power sourcing equipment (PSE)
 * controller.
 */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "pse_ltc4291.h"
#include "string.h"
#include "timer.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "PSE: " format, ##args)

static int pse_write_hpmd(int port, int val)
{
	switch (port) {
	case 0:
		return I2C_PSE_WRITE(HPMD1, val);
	case 1:
		return I2C_PSE_WRITE(HPMD2, val);
	case 2:
		return I2C_PSE_WRITE(HPMD3, val);
	case 3:
		return I2C_PSE_WRITE(HPMD4, val);
	default:
		return EC_ERROR_INVAL;
	}
}

static int pse_port_enable(int port)
{
	/* Enable detection and classification */
	return I2C_PSE_WRITE(DETPB, LTC4291_DETPB_EN_PORT(port));
}

static int pse_port_disable(int port)
{
	/* Request power off (this also disables detection/classification) */
	return I2C_PSE_WRITE(PWRPB, LTC4291_PWRPB_OFF_PORT(port));
}

static int pse_init_worker(void)
{
	timestamp_t deadline;
	int err, id, devid, statpin, port;

	/* Ignore errors -- may already be resetting */
	I2C_PSE_WRITE(RSTPB, LTC4291_FLD_RSTPB_RSTALL);

	deadline.val = get_time().val + LTC4291_RESET_DELAY_US;
	while ((err = I2C_PSE_READ(ID, &id)) != EC_SUCCESS) {
		if (timestamp_expired(deadline, NULL))
			return EC_ERROR_TIMEOUT;
		crec_msleep(1);
	}

	err = I2C_PSE_READ(DEVID, &devid);
	if (err != EC_SUCCESS)
		return err;

	if (id != LTC4291_ID || devid != LTC4291_DEVID)
		return EC_ERROR_INVAL;

	err = I2C_PSE_READ(STATPIN, &statpin);
	if (err != EC_SUCCESS)
		return err;

	/*
	 * We don't want to supply power until we've had a chance to set the
	 * limits.
	 */
	if (statpin & LTC4291_FLD_STATPIN_AUTO)
		CPRINTS("WARN: PSE reset in AUTO mode");

	err = I2C_PSE_WRITE(OPMD, LTC4291_OPMD_AUTO);
	if (err != EC_SUCCESS)
		return err;

	/* Set maximum power each port is allowed to allocate. */
	for (port = 0; port < LTC4291_PORT_MAX; port++) {
		err = pse_write_hpmd(port, pse_port_hpmd[port]);
		if (err != EC_SUCCESS)
			return err;
	}

	err = I2C_PSE_WRITE(DISENA, LTC4291_DISENA_ALL);
	if (err != EC_SUCCESS)
		return err;

	err = I2C_PSE_WRITE(DETENA, LTC4291_DETENA_ALL);
	if (err != EC_SUCCESS)
		return err;

	return EC_SUCCESS;
}

static void pse_init(void)
{
	int err;

	err = pse_init_worker();
	if (err != EC_SUCCESS)
		CPRINTS("PSE init failed: %d", err);
	else
		CPRINTS("PSE init done");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pse_init, HOOK_PRIO_DEFAULT);

/* Also reset the PSE on a reboot to toggle the power. */
DECLARE_HOOK(HOOK_CHIPSET_RESET, pse_init, HOOK_PRIO_DEFAULT);

static int command_pse(int argc, const char **argv)
{
	int port;

	/*
	 *  Initialization does not reliably work after reset because the device
	 *  is held in reset by the AP. Running this command after boot finishes
	 *  always succeeds. Remove once the reset signal changes.
	 */
	if (!strncmp(argv[1], "init", 4))
		return pse_init_worker();

	if (argc != 3)
		return EC_ERROR_PARAM_COUNT;

	port = atoi(argv[1]);
	if (port < 0 || port >= LTC4291_PORT_MAX)
		return EC_ERROR_PARAM1;

	if (!strncmp(argv[2], "off", 3))
		return pse_port_disable(port);
	else if (!strncmp(argv[2], "on", 2))
		return pse_port_enable(port);
	else if (!strncmp(argv[2], "min", 3))
		return pse_write_hpmd(port, LTC4291_HPMD_MIN);
	else if (!strncmp(argv[2], "max", 3))
		return pse_write_hpmd(port, LTC4291_HPMD_MAX);
	else
		return EC_ERROR_PARAM2;
}
DECLARE_CONSOLE_COMMAND(pse, command_pse, "<port# 0-3> <off | on | min | max>",
			"Set PSE port power");

static int ec_command_pse_status(int port, uint8_t *status)
{
	int detena, statpwr;
	int err;

	err = I2C_PSE_READ(DETENA, &detena);
	if (err != EC_SUCCESS)
		return err;

	err = I2C_PSE_READ(STATPWR, &statpwr);
	if (err != EC_SUCCESS)
		return err;

	if ((detena & LTC4291_DETENA_EN_PORT(port)) == 0)
		*status = EC_PSE_STATUS_DISABLED;
	else if ((statpwr & LTC4291_STATPWR_ON_PORT(port)) == 0)
		*status = EC_PSE_STATUS_ENABLED;
	else
		*status = EC_PSE_STATUS_POWERED;

	return EC_SUCCESS;
}

static enum ec_status ec_command_pse(struct host_cmd_handler_args *args)
{
	const struct ec_params_pse *p = args->params;
	int err = 0;

	if (p->port >= LTC4291_PORT_MAX)
		return EC_RES_INVALID_PARAM;

	switch (p->cmd) {
	case EC_PSE_STATUS: {
		struct ec_response_pse_status *r = args->response;

		args->response_size = sizeof(*r);
		err = ec_command_pse_status(p->port, &r->status);
		break;
	}
	case EC_PSE_ENABLE:
		err = pse_port_enable(p->port);
		break;
	case EC_PSE_DISABLE:
		err = pse_port_disable(p->port);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	if (err)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PSE, ec_command_pse, EC_VER_MASK(0));
