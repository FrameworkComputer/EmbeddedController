/* Copyright 2020 The Chromium OS Authors. All rights reserved.
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
#include "string.h"
#include "timer.h"
#include "util.h"

#define LTC4291_I2C_ADDR	0x2C

#define LTC4291_REG_SUPEVN_COR	0x0B
#define LTC4291_REG_STATPWR	0x10
#define LTC4291_REG_STATPIN	0x11
#define LTC4291_REG_OPMD	0x12
#define LTC4291_REG_DISENA	0x13
#define LTC4291_REG_DETENA	0x14
#define LTC4291_REG_DETPB	0x18
#define LTC4291_REG_PWRPB	0x19
#define LTC4291_REG_RSTPB	0x1A
#define LTC4291_REG_ID		0x1B
#define LTC4291_REG_DEVID	0x43
#define LTC4291_REG_HPMD1	0x46
#define LTC4291_REG_HPMD2	0x4B
#define LTC4291_REG_HPMD3	0x50
#define LTC4291_REG_HPMD4	0x55
#define LTC4291_REG_LPWRPB	0x6E

#define LTC4291_FLD_STATPIN_AUTO	BIT(0)
#define LTC4291_FLD_RSTPB_RSTALL	BIT(4)

#define LTC4291_STATPWR_ON_PORT(port)	(0x01 << (port))
#define LTC4291_DETENA_EN_PORT(port)	(0x11 << (port))
#define LTC4291_DETPB_EN_PORT(port)	(0x11 << (port))
#define LTC4291_PWRPB_OFF_PORT(port)	(0x10 << (port))

#define LTC4291_OPMD_AUTO	0xFF
#define LTC4291_DISENA_ALL	0x0F
#define LTC4291_DETENA_ALL	0xFF
#define LTC4291_ID		0x64
#define LTC4291_DEVID		0x38
#define LTC4291_HPMD_MIN	0x00
#define LTC4291_HPMD_MAX	0xA8

#define LTC4291_PORT_MAX	4

#define LTC4291_RESET_DELAY_US	(20 * MSEC)

#define I2C_PSE_READ(reg, data) \
	i2c_read8(I2C_PORT_PSE, LTC4291_I2C_ADDR, LTC4291_REG_##reg, (data))

#define I2C_PSE_WRITE(reg, data) \
	i2c_write8(I2C_PORT_PSE, LTC4291_I2C_ADDR, LTC4291_REG_##reg, (data))

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

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

/*
 * Port 1: 100W
 * Port 2-4: 15W
 */
static int pse_port_hpmd[4] = {
	LTC4291_HPMD_MAX,
	LTC4291_HPMD_MIN,
	LTC4291_HPMD_MIN,
	LTC4291_HPMD_MIN,
};

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
	while ((err = I2C_PSE_READ(ID, &id)) != 0) {
		if (timestamp_expired(deadline, NULL))
			return err;
	}

	err = I2C_PSE_READ(DEVID, &devid);
	if (err != 0)
		return err;

	if (id != LTC4291_ID || devid != LTC4291_DEVID)
		return EC_ERROR_INVAL;

	err = I2C_PSE_READ(STATPIN, &statpin);
	if (err != 0)
		return err;

	/*
	 * We don't want to supply power until we've had a chance to set the
	 * limits.
	 */
	if (statpin & LTC4291_FLD_STATPIN_AUTO)
		CPRINTS("WARN: PSE reset in AUTO mode");

	err = I2C_PSE_WRITE(OPMD, LTC4291_OPMD_AUTO);
	if (err != 0)
		return err;

	/* Set maximum power each port is allowed to allocate. */
	for (port = 0; port < LTC4291_PORT_MAX; port++) {
		err = pse_write_hpmd(port, pse_port_hpmd[port]);
		if (err != 0)
			return err;
	}

	err = I2C_PSE_WRITE(DISENA, LTC4291_DISENA_ALL);
	if (err != 0)
		return err;

	err = I2C_PSE_WRITE(DETENA, LTC4291_DETENA_ALL);
	if (err != 0)
		return err;

	return 0;
}

static void pse_init(void)
{
	int err;

	err = pse_init_worker();
	if (err != 0)
		CPRINTS("PSE init failed: %d", err);
	else
		CPRINTS("PSE init done");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pse_init, HOOK_PRIO_DEFAULT);

static int command_pse(int argc, char **argv)
{
	int port;

	/*
	 *  TODO(b/156399232): endeavour: PSE controller reset by PLTRST
	 *
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
DECLARE_CONSOLE_COMMAND(pse, command_pse,
			"<port# 0-3> <off | on | min | max>",
			"Set PSE port power");

static int ec_command_pse_status(int port, uint8_t *status)
{
	int detena, statpwr;
	int err;

	err = I2C_PSE_READ(DETENA, &detena);
	if (err != 0)
		return err;

	err = I2C_PSE_READ(STATPWR, &statpwr);
	if (err != 0)
		return err;

	if ((detena & LTC4291_DETENA_EN_PORT(port)) == 0)
		*status = EC_PSE_STATUS_DISABLED;
	else if ((statpwr & LTC4291_STATPWR_ON_PORT(port)) == 0)
		*status = EC_PSE_STATUS_ENABLED;
	else
		*status = EC_PSE_STATUS_POWERED;

	return 0;
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
