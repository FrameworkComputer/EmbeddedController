/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dacs.h"
#include "i2c.h"
#include "ioexpanders.h"
#include "util.h"

#define MAX_MV 5000

#define CC1_DAC_ADDR 0x48
#define CC2_DAC_ADDR 0x49

#define REG_NOOP    0
#define REG_DEVID   1
#define REG_SYNC    2
#define REG_CONFIG  3
#define REG_GAIN    4
#define REG_TRIGGER 5
#define REG_STATUS  7
#define REG_DAC     8

#define DAC1	BIT(0)
#define DAC2	BIT(1)

static uint8_t dac_enabled;

void init_dacs(void)
{
	/* Disable both DACS by default */
	enable_dac(CC1_DAC, 0);
	enable_dac(CC2_DAC, 0);
	dac_enabled = 0;
}

void enable_dac(enum dac_t dac, uint8_t en)
{
	switch (dac) {
	case CC1_DAC:
		if (en) {
			fault_clear_cc(1);
			fault_clear_cc(0);
			en_vout_buf_cc1(1);
			/* Power ON DAC */
			i2c_write8(1, CC1_DAC_ADDR, REG_CONFIG, 0);
			dac_enabled |= DAC1;
		} else {
			en_vout_buf_cc1(0);
			/* Power OFF DAC */
			i2c_write8(1, CC1_DAC_ADDR, REG_CONFIG, 1);
			dac_enabled &= ~DAC1;
		}
		break;
	case CC2_DAC:
		if (en) {
			fault_clear_cc(1);
			fault_clear_cc(0);
			en_vout_buf_cc2(1);
			i2c_write8(1, CC2_DAC_ADDR, REG_CONFIG, 0);
			dac_enabled |= DAC2;
		} else {
			en_vout_buf_cc2(0);
			/* Power down DAC */
			i2c_write8(1, CC2_DAC_ADDR, REG_CONFIG, 1);
			dac_enabled &= ~DAC2;
		}
		break;
	}
}

int write_dac(enum dac_t dac, uint16_t value)
{
	uint16_t tmp;

	/*
	 * Data are MSB aligned in straight binary format, and
	 * use the following format: DATA[13:0], 0, 0
	 */
	tmp = (value << 8) & 0xff00;
	tmp |= (value >> 8) & 0xff;
	tmp <<= 2;

	switch (dac) {
	case CC1_DAC:
		if (!(dac_enabled & DAC1)) {
			ccprintf("CC1_DAC is disabled\n");
			return EC_ERROR_ACCESS_DENIED;
		}
		i2c_write16(1, CC1_DAC_ADDR, REG_DAC, tmp);
		break;
	case CC2_DAC:
		if (!(dac_enabled & DAC2)) {
			ccprintf("CC2_DAC is disabled\n");
			return EC_ERROR_ACCESS_DENIED;
		}
		i2c_write16(1, CC2_DAC_ADDR, REG_DAC, tmp);
		break;
	}
	return EC_SUCCESS;
}

static int cmd_cc_dac(int argc, char *argv[])
{
	uint8_t dac;
	uint64_t mv;
	uint64_t round_up;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	dac = strtoi(argv[1], &e, 10);
	if (*e || (dac != CC1_DAC && dac != CC2_DAC))
		return EC_ERROR_PARAM2;

	if (!strcasecmp(argv[2], "on")) {
		enable_dac(dac, 1);
	} else if (!strcasecmp(argv[2], "off")) {
		enable_dac(dac, 0);
	} else {
		/* get value in mV */
		mv = strtoi(argv[2], &e, 10);
		/* 5000 mV max */
		if (*e || mv > MAX_MV)
			return EC_ERROR_PARAM3;
		/* 305176 = (5V / 2^14) * 1000000 */
		/* 152588 = 305176 / 2 : used for round up after division */
		round_up = (((mv * 1000000) + 152588) / 305176);
		if (!write_dac(dac, (uint16_t)round_up))
			ccprintf("Setting DAC to %lld counts\n", round_up);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cc_dac, cmd_cc_dac,
			"dac <\"on\"|\"off\"|mv>",
			"Set Servo v4.1 CC dacs");
