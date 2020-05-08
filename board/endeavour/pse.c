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
#include "i2c.h"
#include "string.h"
#include "timer.h"
#include "util.h"

#define LTC4291_I2C_ADDR	0x2C

#define LTC4291_REG_SUPEVN_COR	0x0B
#define LTC4291_REG_STATPIN	0x11
#define LTC4291_REG_OPMD	0x12
#define LTC4291_REG_DISENA	0x13
#define LTC4291_REG_DETENA	0x14
#define LTC4291_REG_RSTPB	0x1A
#define LTC4291_REG_ID		0x1B
#define LTC4291_REG_DEVID	0x43
#define LTC4291_REG_HPMD1	0x46
#define LTC4291_REG_HPMD2	0x4B
#define LTC4291_REG_HPMD3	0x50
#define LTC4291_REG_HPMD4	0x55

#define LTC4291_FLD_STATPIN_AUTO	BIT(0)
#define LTC4291_FLD_RSTPB_RSTALL	BIT(4)

#define LTC4291_OPMD_AUTO	0xFF
#define LTC4291_DISENA_ALL	0x0F
#define LTC4291_DETENA_ALL	0xFF
#define LTC4291_ID		0x64
#define LTC4291_DEVID		0x38
#define LTC4291_HPMD_MIN	0x00
#define LTC4291_HPMD_MAX	0xA8

#define LTC4291_RESET_DELAY_MS	10

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

static int pse_init_worker(void)
{
	int err, id, devid, statpin, port;

	err = I2C_PSE_WRITE(RSTPB, LTC4291_FLD_RSTPB_RSTALL);
	if (err != 0)
		return err;

	msleep(LTC4291_RESET_DELAY_MS);

	err = I2C_PSE_READ(ID, &id);
	if (err != 0)
		return err;

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
	for (port = 0; port < 4; port++) {
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
DECLARE_HOOK(HOOK_INIT, pse_init, HOOK_PRIO_INIT_I2C);

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
	if (port < 1 || port > 4)
		return EC_ERROR_PARAM1;

	if (!strncmp(argv[2], "off", 3))
		return EC_ERROR_UNIMPLEMENTED;
	else if (!strncmp(argv[2], "min", 3))
		return pse_write_hpmd(port - 1, LTC4291_HPMD_MIN);
	else if (!strncmp(argv[2], "max", 3))
		return pse_write_hpmd(port - 1, LTC4291_HPMD_MAX);
	else
		return EC_ERROR_PARAM2;
}
DECLARE_CONSOLE_COMMAND(pse, command_pse,
			"<port# 1-4> <off | min | max>",
			"Set PSE port power");
