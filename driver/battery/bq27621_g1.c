/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for BQ27621-G1
 */

#include "battery.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"
#include "timer.h"

#define BQ27621_ADDR                        0xaa
#define BQ27621_TYPE_ID                     0x0621

#define REG_CTRL                            0x00
#define REG_TEMPERATURE                     0x02
#define REG_VOLTAGE                         0x04
#define REG_FLAGS                           0x06
#define REG_NOMINAL_CAPACITY                0x08
#define REG_FULL_AVAILABLE_CAPACITY         0x0a
#define REG_REMAINING_CAPACITY              0x0c
#define REG_FULL_CHARGE_CAPACITY            0x0e
#define REG_EFFECTIVE_CURRENT               0x10
#define REG_AVERAGE_POWER                   0x18
#define REG_STATE_OF_CHARGE                 0x1c
#define REG_INTERNAL_TEMPERATURE            0x1e
#define REG_REMAINING_CAPACITY_UNFILTERED   0x28
#define REG_REMAINING_CAPACITY_FILTERED     0x2a
#define REG_FULL_CHARGE_CAPACITY_UNFILTERED 0x28
#define REG_FULL_CHARGE_CAPACITY_FILTERED   0x2a
#define REG_STATE_OF_CHARGE_UNFILTERED      0x30
#define REG_OP_CONFIG                       0x3a
#define REG_DESIGN_CAPACITY                 0x3c
#define REG_DATA_CLASS                      0x3e
#define REG_DATA_BLOCK                      0x3f
#define REG_BLOCK_DATA_CHECKSUM             0x60
#define REG_BLOCK_DATA_CONTROL              0x61

#define REGISTERS_BLOCK_OFFSET                64
#define REGISTERS_BLOCK_OP_CONFIG           0x40
#define REGISTERS_BLOCK_OP_CONFIG_B         0x42
#define REGISTERS_BLOCK_DF_VERSION          0x43

/* State block */
#define STATE_BLOCK_OFFSET                    82
#define STATE_BLOCK_DESIGN_CAPACITY         0x43
#define STATE_BLOCK_DESIGN_ENERGY           0x45
#define STATE_BLOCK_TERMINATE_VOLTAGE       0x49
#define STATE_BLOCK_TAPER_RATE              0x54

/* BQ27621 Control subcommands */
#define CONTROL_CONTROL_STATUS   0x00
#define CONTROL_DEVICE_TYPE      0x01
#define CONTROL_FW_VERSION       0x02
#define CONTROL_PREV_MACWRITE    0x07
#define CONTROL_CHEM_ID          0x08
#define CONTROL_BAT_INSERT       0x0C
#define CONTROL_BAT_REMOVE       0x0D
#define CONTROL_TOGGLE_POWERMIN  0x10
#define CONTROL_SET_HIBERNATE    0x11
#define CONTROL_CLEAR_HIBERNATE  0x12
#define CONTROL_SET_CFGUPDATE    0x13
#define CONTROL_SHUTDOWN_ENABLE  0x1B
#define CONTROL_SHUTDOWN         0x1C
#define CONTROL_SEALED           0x20
#define CONTROL_TOGGLE_GPOUT     0x23
#define CONTROL_ALT_CHEM1        0x31
#define CONTROL_ALT_CHEM2        0x32
#define CONTROL_RESET            0x41
#define CONTROL_SOFT_RESET       0x42
#define CONTROL_EXIT_CFGUPDATE   0x43
#define CONTROL_EXIT_RESIM       0x44
#define CONTROL_UNSEAL           0x8000

/* BQ27621 Status bits */
#define STATUS_SHUTDOWNEN        0x8000
#define STATUS_WDRESET           0x4000
#define STATUS_SS                0x2000
#define STATUS_CALMODE           0x1000
#define STATUS_OCVCMDCOMP        0x0200
#define STATUS_OCVFAIL           0x0100
#define STATUS_INITCOMP          0x0080
#define STATUS_HIBERNATE         0x0040
#define STATUS_POWERMIN          0x0020
#define STATUS_SLEEP             0x0010
#define STATUS_LDMD              0x0008
#define STATUS_CHEMCHNG          0x0001

/* BQ27621 Flags bits */
#define FLAGS_OT                 0x8000
#define FLAGS_UT                 0x4000
#define FLAGS_FC                 0x0200
#define FLAGS_CHG                0x0100
#define FLAGS_OCVTAKEN           0x0080
#define FLAGS_ITPOR              0x0020
#define FLAGS_CFGUPD             0x0010
#define FLAGS_BAT_DET            0x0008
#define FLAGS_SOC1               0x0004
#define FLAGS_SOCF               0x0002
#define FLAGS_DSG                0x0001

/*
 * There are some parameters that need to be defined in the board file:
 * BQ27621_TOGGLE_POWER_MIN -  Put it in minimum power mode
 *    (may affect I2C timing)
 * BQ27621_DESIGN_CAPACITY -   mAh
 * BQ27621_DESIGN_ENERGY -     Design Capacity x 3.7
 * BQ27621_TERMINATE_VOLTAGE - mV
 * BQ27621_TAPER_CURRENT -     mA
 * BQ27621_CHEM_ID -           0x1202 (DEFAULT) 0x1210 (ALT_CHEM1)
 *                             0x354 (ALT_CHEM2)
 *
 * For extra large or extra small batteries, this driver scales everything but
 * voltages.  The recommended range is 150mAh - 6Ah
 *
 */

#define BQ27621_SCALE_FACTOR (BQ27621_DESIGN_CAPACITY < 150 ? 10.0 : \
				(BQ27621_DESIGN_CAPACITY > 6000 ? 0.1 : 1))

#define BQ27621_UNSCALE(x)   (BQ27621_SCALE_FACTOR == 10 ? (x) / 10 : \
				(BQ27621_SCALE_FACTOR == 0.1 ? (x) * 10 : (x)))

#define BQ27621_TAPER_RATE  ((int)(BQ27621_DESIGN_CAPACITY/    \
				(0.1 * BQ27621_TAPER_CURRENT)))

#define BQ27621_SCALED_DESIGN_CAPACITY ((int)(BQ27621_DESIGN_CAPACITY /   \
					BQ27621_SCALE_FACTOR))
#define BQ27621_SCALED_DESIGN_ENERGY   ((int)(BQ27621_DESIGN_CAPACITY /   \
					BQ27621_SCALE_FACTOR))

/*
 *Everything is LSB first.  Parameters need to be converted.
 *
 * The values from the data sheet are already LSB-first.
 */

#define ENDIAN_SWAP_2B(x)     ((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8))
#define DESIGN_CAPACITY       ENDIAN_SWAP_2B(BQ27621_SCALED_DESIGN_CAPACITY)
#define DESIGN_ENERGY         ENDIAN_SWAP_2B(BQ27621_SCALED_DESIGN_ENERGY)
#define TAPER_RATE            ENDIAN_SWAP_2B(BQ27621_TAPER_RATE)
#define TERMINATE_VOLTAGE     ENDIAN_SWAP_2B(BQ27621_TERMINATE_VOLTAGE)

struct battery_info battery_params;

static int bq27621_read(int offset, int *data)
{
	return i2c_read16(I2C_PORT_BATTERY, BQ27621_ADDR, offset, data);
}

static int bq27621_read8(int offset, int *data)
{
	return i2c_read8(I2C_PORT_BATTERY, BQ27621_ADDR, offset, data);
}

static int bq27621_write(int offset, int data)
{
	return i2c_write16(I2C_PORT_BATTERY, BQ27621_ADDR, offset, data);
}

static int bq27621_write8(int offset, int data)
{
	return i2c_write8(I2C_PORT_BATTERY, BQ27621_ADDR, offset, data);
}

static int bq27621_probe(void)
{
	int rv;
	int battery_type_id;

	/* Delays need to be added for correct operation at > 100Kbps */
	ASSERT(i2c_ports[I2C_PORT_BATTERY].kbps <= 100);

	rv = bq27621_write(REG_CTRL, CONTROL_DEVICE_TYPE);
	rv |= bq27621_read(REG_CTRL, &battery_type_id);

	if (rv)
		return rv;
	if (battery_type_id == BQ27621_TYPE_ID) {
		battery_params.voltage_max = BATTERY_VOLTAGE_MAX;
		battery_params.voltage_normal = BATTERY_VOLTAGE_NORMAL;
		battery_params.voltage_min = BATTERY_VOLTAGE_MIN;
		return EC_SUCCESS;
	}
	return EC_ERROR_UNKNOWN;
}

static inline int bq27621_unseal(void)
{
	return bq27621_write(REG_CTRL, CONTROL_UNSEAL) |
				bq27621_write(REG_CTRL, CONTROL_UNSEAL);
}

static int bq27621_enter_config_update(void)
{
	int tries, flags = 0, rv = EC_SUCCESS;

	/* Enter Config Update Mode (Can take up to a second) */
	for (tries = 2000; tries > 0 && !(flags & FLAGS_CFGUPD) &&
					(rv == EC_SUCCESS); tries--) {
		rv |= bq27621_write(REG_CTRL, CONTROL_SET_CFGUPDATE);
		rv |= bq27621_read(REG_FLAGS, &flags);
	}

	if (tries == 0)
		return EC_ERROR_TIMEOUT;
	else
		return EC_SUCCESS;
}

static int bq27621_enter_block_mode(int block)
{
	int rv;
	rv = bq27621_write8(REG_BLOCK_DATA_CONTROL, 0);
	rv |= bq27621_write8(REG_DATA_CLASS, block);
	rv |= bq27621_write8(REG_DATA_BLOCK, 0);
	udelay(500); /* Shouldn't be needed, doesn't work without it. */
	return rv;
}

static int bq27621_seal(void)
{
	int rv = 0;
	int status = 0, param = 0, checksum = 0;

	rv |= bq27621_write(REG_CTRL, CONTROL_CONTROL_STATUS);
	rv |= bq27621_read(REG_CTRL, &status);

	if (status & STATUS_SS) /* Already sealed */
		return EC_SUCCESS;

	/* Enter Config Update Mode */
	rv = bq27621_enter_config_update();

	if (rv)
		return rv;

	/* Set up block RAM update */
	rv = bq27621_enter_block_mode(REGISTERS_BLOCK_OFFSET);

	if (rv)
		return rv;

	rv = bq27621_read8(REG_BLOCK_DATA_CHECKSUM, &checksum);

	if (rv)
		return rv;

	checksum = 0xff - checksum;

	rv = bq27621_read8(REGISTERS_BLOCK_OP_CONFIG_B, &param);
	checksum -= param; /* 1B */

	param |= 1<<5; /* Set DEF_SEAL */

	rv = bq27621_write8(REGISTERS_BLOCK_OP_CONFIG_B, param);
	checksum += param; /* 1B */

	checksum = 0xff - (0xff & checksum);

	rv = bq27621_write8(REG_BLOCK_DATA_CHECKSUM, checksum);

	if (rv)
		return rv;

	/* Exit Update */
	rv = bq27621_write(REG_CTRL, CONTROL_SOFT_RESET);

	return rv;
}

#define CHECKSUM_2B(x) ((x & 0xff) + ((x>>8) & 0xff))

static int bq27621_init(void)
{
	int rv;
	int status = 0, param = 0, checksum = 0;

	rv = bq27621_probe();

	if (rv)
		return rv;

	/* Unseal the device if necessary */
	rv |= bq27621_write(REG_CTRL, CONTROL_CONTROL_STATUS);
	rv |= bq27621_read(REG_CTRL, &status);

	if (status & STATUS_SS)
		rv |= bq27621_unseal();

	if (rv)
		return rv;

	/* Select the alternate chemistry if needed */
	rv = bq27621_write(REG_CTRL, CONTROL_CHEM_ID);
	rv |= bq27621_read(REG_CTRL, &param);

	if (param != BQ27621_CHEM_ID) { /* Change needed */

		if (BQ27621_CHEM_ID == 0x1202) { /* Return to default */
			rv |= bq27621_write(REG_CTRL, CONTROL_RESET);
		} else {
			rv |= bq27621_enter_config_update();

			if (BQ27621_CHEM_ID == 0x1210)
				rv |= bq27621_write(REG_CTRL,
					CONTROL_ALT_CHEM1);
			if (BQ27621_CHEM_ID == 0x0354)
				rv |= bq27621_write(REG_CTRL,
					CONTROL_ALT_CHEM2);

		/*
		 * The datasheet recommends checking the status here.
		 *
		 * If the CHEMCHG is active, it wasn't successful.
		 *
		 * There's no recommendation for what to do if it isn't.
		 */

			rv |= bq27621_write(REG_CTRL, CONTROL_EXIT_CFGUPDATE);
		}
	}

	if (rv)
		return rv;

	rv = bq27621_enter_config_update();

	if (rv)
		return rv;

	/* Set up block RAM update */
	rv = bq27621_enter_block_mode(STATE_BLOCK_OFFSET);

	if (rv)
		return rv;

	rv = bq27621_read8(REG_BLOCK_DATA_CHECKSUM, &checksum);
	if (rv)
		return rv;

	checksum = 0xff - checksum;

	rv = bq27621_read(STATE_BLOCK_DESIGN_CAPACITY, &param);
	checksum -= CHECKSUM_2B(param);

	rv |= bq27621_read(STATE_BLOCK_DESIGN_ENERGY, &param);
	checksum -= CHECKSUM_2B(param);

	rv |= bq27621_read(STATE_BLOCK_TERMINATE_VOLTAGE, &param);
	checksum -= CHECKSUM_2B(param);

	rv |= bq27621_read(STATE_BLOCK_TAPER_RATE, &param);
	checksum -= CHECKSUM_2B(param);

	if (rv)
		return rv;

	rv = bq27621_write(STATE_BLOCK_DESIGN_CAPACITY, DESIGN_CAPACITY);
	checksum += CHECKSUM_2B(DESIGN_CAPACITY);

	rv |= bq27621_write(STATE_BLOCK_DESIGN_ENERGY, DESIGN_ENERGY);
	checksum += CHECKSUM_2B(DESIGN_ENERGY);

	rv |= bq27621_write(STATE_BLOCK_TERMINATE_VOLTAGE, TERMINATE_VOLTAGE);
	checksum += CHECKSUM_2B(TERMINATE_VOLTAGE);

	rv |= bq27621_write(STATE_BLOCK_TAPER_RATE, TAPER_RATE);
	checksum += CHECKSUM_2B(TAPER_RATE);

	checksum = 0xff - (0xff & checksum);


	if (rv)
		return rv;

	rv = bq27621_write8(REG_BLOCK_DATA_CHECKSUM, checksum);

	rv |= bq27621_write(REG_CTRL, CONTROL_SOFT_RESET);

	if (rv)
		return rv;

	bq27621_seal();

	return rv;
}

static void probe_type_id_init(void)
{
	int rv = EC_SUCCESS;

	rv = bq27621_probe();

	if (rv)
		return;

	rv = bq27621_init();

	if (rv) { /* Try it once more */
		rv = bq27621_write(REG_CTRL, CONTROL_RESET);
	  rv |= bq27621_init();
	}
}

DECLARE_HOOK(HOOK_INIT, probe_type_id_init, HOOK_PRIO_DEFAULT);

/* Some of the functions to make this battery "smart" */

int battery_device_name(char *device_name, int buf_size)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_state_of_charge_abs(int *percent)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_remaining_capacity(int *capacity)
{
	int scaled_value, err_code;

	err_code = bq27621_read(REG_REMAINING_CAPACITY, &scaled_value);

	*capacity = BQ27621_UNSCALE(scaled_value);

	return err_code;
}

int battery_full_charge_capacity(int *capacity)
{
	int scaled_value, err_code;

	err_code = bq27621_read(REG_FULL_CHARGE_CAPACITY, &scaled_value);

	*capacity = BQ27621_UNSCALE(scaled_value);

	return err_code;
}

int battery_time_to_empty(int *minutes)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_time_to_full(int *minutes)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_cycle_count(int *count)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_design_capacity(int *capacity)
{
	int scaled_value, err_code;

	err_code = bq27621_read(REG_DESIGN_CAPACITY, &scaled_value);

	*capacity = BQ27621_UNSCALE(scaled_value);

	return err_code;
}

int battery_time_at_rate(int rate, int *minutes)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_manufacturer_name(char *dest, int size)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_device_chemistry(char *dest, int size)
{
	uint32_t rv;
	int param;

	rv = bq27621_write(REG_CTRL, CONTROL_CHEM_ID);
	rv |= bq27621_read(REG_CTRL, &param);

	if (param == 0x1202)
		strzcpy(dest, "0x1202 (default)", size);
	if (param == 0x1210)
		strzcpy(dest, "0x1210 (ALT_CHEM1)", size);
	if (param == 0x0354)
		strzcpy(dest, "0x0354 (ALT_CHEM2)", size);

	return EC_SUCCESS;
}

int battery_serial_number(int *serial)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_design_voltage(int *voltage)
{
	*voltage = BATTERY_VOLTAGE_NORMAL;

	return EC_SUCCESS;
}

int battery_get_mode(int *mode)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_status(int *status)
{
	return EC_ERROR_UNIMPLEMENTED;
}

enum battery_present battery_is_present(void)
{
	return EC_ERROR_UNIMPLEMENTED;
}

void battery_get_params(struct batt_params *batt)
{
	/* Reset flags */
	batt->flags = 0;

	if (bq27621_read(REG_TEMPERATURE, &batt->temperature))
		batt->flags |= BATT_FLAG_BAD_TEMPERATURE;
	else
		batt->flags |= BATT_FLAG_RESPONSIVE; /* Battery is responding */

	if (bq27621_read8(REG_STATE_OF_CHARGE, &batt->state_of_charge))
		batt->flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	if (bq27621_read(REG_VOLTAGE, &batt->voltage))
		batt->flags |= BATT_FLAG_BAD_VOLTAGE;

	batt->flags |= BATT_FLAG_BAD_CURRENT;
	batt->current = 0;

	/* Default to not desiring voltage and current */
	batt->desired_voltage = batt->desired_current = 0;
}

/* Wait until battery is totally stable */
int battery_wait_for_stable(void)
{
	/* TODO(crosbug.com/p/30426): implement me */
	return EC_SUCCESS;
}

#ifdef CONFIG_CMD_BATDEBUG
	#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)
#else
	#define CPRINTF(format, args...)
#endif

#ifdef CONFIG_CMD_BATDEBUG

static int command_fgunseal(int argc, char **argv)
{
	int rv = EC_SUCCESS;

	if (argc > 1)
		return EC_ERROR_PARAM_COUNT;

	rv = bq27621_unseal();

	return rv;
}

DECLARE_CONSOLE_COMMAND(fgunseal, command_fgunseal,
			"",
			"Unseal the fg",
			NULL);

static int command_fgseal(int argc, char **argv)
{
	int rv = EC_SUCCESS;

	if (argc > 1)
		return EC_ERROR_PARAM_COUNT;

	rv = bq27621_seal();

	return rv;
}

DECLARE_CONSOLE_COMMAND(fgseal, command_fgseal,
			"",
			"Seal the fg",
			NULL);

static int command_fginit(int argc, char **argv)
{
	int rv = EC_SUCCESS;
	int force = 0;
	int flags = 0;
	int unconfigured = 0;
	char *e;

	if (argc > 2)
		return EC_ERROR_PARAM_COUNT;

	if (argc == 2) {
		force = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	rv = bq27621_read(REG_FLAGS, &flags);
	unconfigured = flags & FLAGS_ITPOR;

	if (!unconfigured && force) {
		rv |= bq27621_write(REG_CTRL, CONTROL_RESET);
		unconfigured = (rv == EC_SUCCESS);
	}

	if (unconfigured)
		rv |= bq27621_init();

	return rv;
}

DECLARE_CONSOLE_COMMAND(fginit, command_fginit,
			"[force]",
			"Initialize the fg",
			NULL);

static int command_fgprobe(int argc, char **argv)
{
	int rv = EC_SUCCESS;

	if (argc != 1)
		return EC_ERROR_PARAM_COUNT;

	rv = bq27621_probe();

	return rv;
}

DECLARE_CONSOLE_COMMAND(fgprobe, command_fgprobe,
			"",
			"Probe the fg",
			NULL);

static int command_fgrd(int argc, char **argv)
{
	int cmd, len;
	int rv = EC_SUCCESS;
	int data;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	cmd = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	len = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	if (len == 2)
		rv = bq27621_read(cmd, &data);
	else if (len == 1)
		rv = bq27621_read8(cmd, &data);
	else
		return EC_ERROR_PARAM2;

	CPRINTF("Read %d bytes @0xaa %0x: 0x%0x\n", len, cmd, data);

	return rv;
}

DECLARE_CONSOLE_COMMAND(fgrd, command_fgrd,
			"cmd len",
			"Read _len_ words from the fg",
			NULL);

static int command_fgcmd(int argc, char **argv)
{
	int cmd, data, byte = 0;
	char *e;

	if (argc < 3 || argc > 4)
		return EC_ERROR_PARAM_COUNT;

	cmd = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	data = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	if (argc >= 4) {
		byte = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;
	}

	if (byte) {
		CPRINTF("Write a byte @0xaa %0x: 0x%0x\n", cmd, data);
		return bq27621_write8(cmd, data);
	} else {
		CPRINTF("Write 2 bytes @0xaa %0x: 0x%0x\n", cmd, data);
		return bq27621_write(cmd, data);
	}

}

DECLARE_CONSOLE_COMMAND(fgcmd, command_fgcmd,
			"cmd data [byte]",
			"Send a cmd to the fg",
			NULL);

static int command_fgcmdrd(int argc, char **argv)
{
	int cmd, data, val;
	int rv = EC_SUCCESS;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	cmd = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	data = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	rv = bq27621_write(cmd, data);
	rv |= bq27621_read(cmd, &val);

	CPRINTF("Read: @0xaa (%x %x) %x\n", cmd, data, val);
	return rv;
}

DECLARE_CONSOLE_COMMAND(fgcmdrd, command_fgcmdrd,
			"cmd data",
			"Send a 2-byte cmd to the fg, read back the 2-byte result",
			NULL);

#endif /* CONFIG_CMD_BATDEBUG */

