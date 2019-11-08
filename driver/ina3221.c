/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI INA3221 Power monitor driver.
 */

#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "timer.h"
#include "ina3221.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

const static uint8_t ina3221_reg_map[INA3221_CHAN_COUNT][INA3221_MAX_REG] = {
{ 1, 2,  7,  8 }, /* Chan 1 */
{ 3, 4,  9, 10 }, /* Chan 2 */
{ 5, 6, 11, 12 }  /* Chan 3 */
};

static uint16_t ina3221_read(unsigned int unit, uint8_t reg)
{
	int res;
	int val;

	res = i2c_read16(ina3221[unit].port, ina3221[unit].address,
			 reg, &val);
	if (res) {
		CPRINTS("INA3221 I2C read failed");
		return 0x0bad;
	}
	return (val >> 8) | ((val & 0xff) << 8);
}

static uint16_t ina3221_chan_read(unsigned int unit, enum ina3221_channel chan,
			   enum ina3221_register reg)
{
	if (chan >= INA3221_CHAN_COUNT || reg >= INA3221_MAX_REG) {
		CPRINTS("INA3221 Bad channel or register value");
		return 0x0bad;
	}
	return ina3221_read(unit, ina3221_reg_map[chan][reg]);
}

static int ina3221_write(unsigned int unit, uint8_t reg, uint16_t val)
{
	int res;
	uint16_t be_val = (val >> 8) | ((val & 0xff) << 8);

	res = i2c_write16(ina3221[unit].port, ina3221[unit].address,
			  reg, be_val);
	if (res)
		CPRINTS("INA3221 I2C write failed");
	return res;
}

static void ina3221_init(void)
{
	unsigned int unit;

	for (unit = 0; unit < ina3221_count; unit++) {
		uint16_t conf = INA3221_CONFIG_BASE;
		int chan;

		for (chan = 0; chan < INA3221_CHAN_COUNT; chan++) {
			/* Only initialise if channel is used */
			if (ina3221[unit].name[chan] != NULL)
				conf |= 0x4000 >> chan;
		}
		ina3221_write(unit, INA3221_REG_CONFIG, conf);
	}
}

DECLARE_HOOK(HOOK_INIT, ina3221_init, HOOK_PRIO_INIT_EXTPOWER + 1);

#ifdef CONFIG_CMD_INA
static void ina3221_dump(unsigned int unit)
{
	uint16_t cfg;
	int16_t sv[INA3221_CHAN_COUNT];
	uint16_t bv[INA3221_CHAN_COUNT];
	uint16_t crit[INA3221_CHAN_COUNT];
	uint16_t warn[INA3221_CHAN_COUNT];
	uint16_t mask;
	int chan;

	cfg = ina3221_read(unit, INA3221_REG_CONFIG);
	for (chan = 0; chan < INA3221_CHAN_COUNT; chan++) {
		if (ina3221[unit].name[chan] != NULL) {
			sv[chan] = ina3221_chan_read(unit, chan,
						     INA3221_SHUNT_VOLT);
			bv[chan] = ina3221_chan_read(unit, chan,
						     INA3221_BUS_VOLT);
			crit[chan] = ina3221_chan_read(unit, chan,
						       INA3221_CRITICAL);
			warn[chan] = ina3221_chan_read(unit, chan,
						       INA3221_WARNING);
		}
	}
	mask = ina3221_read(unit, INA3221_REG_MASK);

	ccprintf("Unit %d, address: %04x\n", unit, ina3221[unit].address);
	ccprintf("Configuration   : %04x\n", cfg);
	for (chan = 0; chan < INA3221_CHAN_COUNT; chan++) {
		if (ina3221[unit].name[chan] != NULL) {
			ccprintf("%d: %s:\n", chan, ina3221[unit].name[chan]);
			ccprintf("      Shunt voltage: %04x => %d uV\n",
				sv[chan], INA3221_SHUNT_UV((int)sv[chan]));
			ccprintf("      Bus voltage  : %04x => %d mV\n",
				bv[chan], INA3221_BUS_MV((int)bv[chan]));
			ccprintf("      Warning      : %04x\n", warn[chan]);
			ccprintf("      Critical     : %04x\n", crit[chan]);
		}
	}
	ccprintf("Mask/Enable  : %04x\n", mask);
}

/*****************************************************************************/
/* Console commands */

static int command_ina(int argc, char **argv)
{
	char *e;
	unsigned int unit;
	uint16_t val;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	unit = strtoi(argv[1], &e, 10);
	if (*e || unit >= ina3221_count)
		return EC_ERROR_PARAM1;

	if (argc == 2) { /* dump all registers */
		ina3221_dump(unit);
		return EC_SUCCESS;
	} else if (argc == 4) {
		val = strtoi(argv[3], &e, 16);
		if (*e)
			return EC_ERROR_PARAM3;

		if (!strcasecmp(argv[2], "config")) {
			ina3221_write(unit, INA3221_REG_CONFIG, val);
		} else if (!strcasecmp(argv[2], "mask")) {
			ina3221_write(unit, INA3221_REG_MASK, val);
		} else {
			ccprintf("Invalid register: %s\n", argv[1]);
			return EC_ERROR_INVAL;
		}
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}
DECLARE_CONSOLE_COMMAND(ina, command_ina,
			"<index> [config|mask <val>]",
			"INA3221 voltage sensing");
#endif
