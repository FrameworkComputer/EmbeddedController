/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Placeholder values for temporary battery pack.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bd9995x.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/* Shutdown mode parameter to write to manufacturer access register */
#define SB_SHUTDOWN_DATA 0x0010

/* Vendor CTO command parameter */
#define SB_VENDOR_PARAM_CTO_DISABLE 0
/* Flash address of Enabled Protections C Regsiter */
#define SB_VENDOR_ENABLED_PROTECT_C 0x482C
/* Expected CTO disable value */
#define EXPECTED_CTO_DISABLE_VALUE 0x05

/* Vendor OTD Recovery Temperature command parameter */
#define SB_VENDOR_PARAM_OTD_RECOVERY_TEMP 1
/* Flash address of OTD Recovery Temperature Register */
#define SB_VENDOR_OTD_RECOVERY_TEMP 0x486F
/* Expected OTD recovery temperature in 0.1C */
#define EXPECTED_OTD_RECOVERY_TEMP 400

enum battery_type {
	BATTERY_LG,
	BATTERY_LISHEN,
	BATTERY_SIMPLO,
	BATTERY_TYPE_COUNT,
};

struct eve_batt_params {
	const char *manuf_name;
	const struct battery_info *batt_info;
};

/*
 * Set LISHEN as default since the LG precharge current level could cause the
 * LISHEN battery to not accept charge when it's recovering from a fully
 * discharged state.
 */
#define DEFAULT_BATTERY_TYPE BATTERY_LISHEN
static enum battery_present batt_pres_prev = BP_NOT_SURE;
static enum battery_type board_battery_type = BATTERY_TYPE_COUNT;

/* Battery may delay reporting battery present */
static int battery_report_present = 1;

/*
 * Battery protect_c register value.
 * Because this value can only be read when the battery is unsealed, the read of
 * this register is only done if the value is changed.
 */
static int protect_c_reg = -1;

/*
 * Battery OTD recovery temperature register value.
 * Because this value can only be read when the battery is unsealed, the read of
 * this register is only done if the value is changed.
 */
static int otd_recovery_temp_reg = -1;

/*
 * Battery info for LG A50. Note that the fields start_charging_min/max and
 * charging_min/max are not used for the Eve charger. The effective temperature
 * limits are given by discharging_min/max_c.
 */
static const struct battery_info batt_info_lg = {
	.voltage_max = TARGET_WITH_MARGIN(8800, 5), /* mV */
	.voltage_normal = 7700,
	.voltage_min = 6100, /* Add 100mV for charger accuracy */
	.precharge_current = 256, /* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 46,
	.charging_min_c = 10,
	.charging_max_c = 50,
	.discharging_min_c = 0,
	.discharging_max_c = 60,
};

/*
 * Battery info for LISHEN. Note that the fields start_charging_min/max and
 * charging_min/max are not used for the Eve charger. The effective temperature
 * limits are given by discharging_min/max_c.
 */
static const struct battery_info batt_info_lishen = {
	.voltage_max = TARGET_WITH_MARGIN(8800, 5), /* mV */
	.voltage_normal = 7700,
	.voltage_min = 6100, /* Add 100mV for charger accuracy */
	.precharge_current = 256, /* mA */
	.start_charging_min_c = 0,
	.start_charging_max_c = 46,
	.charging_min_c = 10,
	.charging_max_c = 50,
	.discharging_min_c = 0,
	.discharging_max_c = 60,
};

static const struct eve_batt_params info[] = {
	[BATTERY_LG] = {
		.manuf_name = "LG A50",
		.batt_info = &batt_info_lg,
	},

	[BATTERY_LISHEN] = {
		.manuf_name = "Lishen A50",
		.batt_info = &batt_info_lishen,
	},

	[BATTERY_SIMPLO] = {
		.manuf_name = "Simplo A50",
		.batt_info = &batt_info_lishen,
	},

};
BUILD_ASSERT(ARRAY_SIZE(info) == BATTERY_TYPE_COUNT);

/* Get type of the battery connected on the board */
static int board_get_battery_type(void)
{
	char name[3];
	int i;

	if (!battery_manufacturer_name(name, sizeof(name))) {
		for (i = 0; i < BATTERY_TYPE_COUNT; i++) {
			if (!strncasecmp(name, info[i].manuf_name,
					 ARRAY_SIZE(name) - 1)) {
				board_battery_type = i;
				break;
			}
		}
	}

	return board_battery_type;
}

/*
 * Initialize the battery type for the board.
 *
 * Very first battery info is called by the charger driver to initialize
 * the charger parameters hence initialize the battery type for the board
 * as soon as the I2C is initialized.
 */
static void board_init_battery_type(void)
{
	if (board_get_battery_type() != BATTERY_TYPE_COUNT)
		CPRINTS("found batt: %s", info[board_battery_type].manuf_name);
	else
		CPRINTS("battery not found");
}
DECLARE_HOOK(HOOK_INIT, board_init_battery_type, HOOK_PRIO_INIT_I2C + 1);

const struct battery_info *battery_get_info(void)
{
	return info[board_battery_type == BATTERY_TYPE_COUNT ?
			    DEFAULT_BATTERY_TYPE :
			    board_battery_type]
		.batt_info;
}

int board_cut_off_battery(void)
{
	int rv;

	/* Ship mode command must be sent twice to take effect */
	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;

	rv = sb_write(SB_MANUFACTURER_ACCESS, SB_SHUTDOWN_DATA);
	return rv ? EC_RES_ERROR : EC_RES_SUCCESS;
}

static int charger_should_discharge_on_ac(struct charge_state_data *curr)
{
	/* Can not discharge on AC without battery */
	if (curr->batt.is_present != BP_YES)
		return 0;

	/* Do not discharge on AC if the battery is still waking up */
	if (!(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	    !(curr->batt.status & STATUS_FULLY_CHARGED))
		return 0;

	/*
	 * In light load (<450mA being withdrawn from VSYS) the DCDC of the
	 * charger operates intermittently i.e. DCDC switches continuously
	 * and then stops to regulate the output voltage and current, and
	 * sometimes to prevent reverse current from flowing to the input.
	 * This causes a slight voltage ripple on VSYS that falls in the
	 * audible noise frequency (single digit kHz range). This small
	 * ripple generates audible noise in the output ceramic capacitors
	 * (caps on VSYS and any input of DCDC under VSYS).
	 *
	 * To overcome this issue enable the battery learning operation
	 * and suspend USB charging and DC/DC converter.
	 */
	if (!battery_is_cut_off() &&
	    !(curr->batt.flags & BATT_FLAG_WANT_CHARGE) &&
	    (curr->batt.status & STATUS_FULLY_CHARGED))
		return 1;

	/*
	 * To avoid inrush current from the external charger, enable
	 * discharge on AC 2till the new charger is detected and charge
	 * detect delay has passed.
	 */
	if (!chg_ramp_is_detected() && curr->batt.state_of_charge > 2)
		return 1;

	return 0;
}

int charger_profile_override(struct charge_state_data *curr)
{
	const struct battery_info *batt_info;
	/* battery temp in 0.1 deg C */
	int bat_temp_c = curr->batt.temperature - 2731;
	int disch_on_ac = charger_should_discharge_on_ac(curr);

	charger_discharge_on_ac(disch_on_ac);

	if (disch_on_ac) {
		curr->state = ST_DISCHARGE;
		return 0;
	}

	batt_info = battery_get_info();
	/* Don't charge if outside of allowable temperature range */
	if (bat_temp_c >= batt_info->charging_max_c * 10 ||
	    bat_temp_c < batt_info->charging_min_c * 10) {
		curr->requested_current = 0;
		curr->requested_voltage = 0;
		curr->batt.flags &= ~BATT_FLAG_WANT_CHARGE;
		curr->state = ST_IDLE;
	}
	return 0;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}

enum battery_present battery_hw_present(void)
{
	/* The GPIO is low when the battery is physically present */
	return gpio_get_level(GPIO_BATTERY_PRESENT_L) ? BP_NO : BP_YES;
}

static int battery_init(void)
{
	int batt_status;

	return battery_status(&batt_status) ?
		       0 :
		       !!(batt_status & STATUS_INITIALIZED);
}

/* Allow booting now that the battery has woke up */
static void battery_now_present(void)
{
	CPRINTS("battery will now report present");
	battery_report_present = 1;
}
DECLARE_DEFERRED(battery_now_present);

/*
 * Check for case where XDSG bit is set indicating that even
 * though the FG can be read from the battery, the battery is not able to be
 * charged or discharged. This situation will happen if a battery disconnect was
 * intiaited via H1 setting the DISCONN signal to the battery. This will put the
 * battery pack into a sleep state and when power is reconnected, the FG can be
 * read, but the battery is still not able to provide power to the system. The
 * calling function returns batt_pres = BP_NO, which instructs the charging
 * state machine to prevent powering up the AP on battery alone which could lead
 * to a brownout event when the battery isn't able yet to provide power to the
 * system. .
 */
static int battery_check_disconnect(void)
{
	int rv;
	uint8_t data[6];

	/* Check if battery discharging is disabled. */
	rv = sb_read_mfgacc(PARAM_OPERATION_STATUS, SB_ALT_MANUFACTURER_ACCESS,
			    data, sizeof(data));
	if (rv)
		return BATTERY_DISCONNECT_ERROR;

	if (data[3] & BATTERY_DISCHARGING_DISABLED)
		return BATTERY_DISCONNECTED;

	return BATTERY_NOT_DISCONNECTED;
}

/*
 * Physical detection of battery.
 */
enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres;
	static int battery_report_present_timer_started;

	/* Get the physical hardware status */
	batt_pres = battery_hw_present();

	/*
	 * Make sure battery status is implemented, I2C transactions are
	 * success & the battery status is Initialized to find out if it
	 * is a working battery and it is not in the cut-off mode.
	 *
	 * If battery I2C fails but VBATT is high, battery is booting from
	 * cut-off mode.
	 *
	 * FETs are turned off after Power Shutdown time.
	 * The device will wake up when a voltage is applied to PACK.
	 * Battery status will be inactive until it is initialized.
	 */
	if (batt_pres == BP_YES && batt_pres_prev != batt_pres &&
	    (battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL ||
	     battery_check_disconnect() != BATTERY_NOT_DISCONNECTED ||
	     battery_init() == 0)) {
		battery_report_present = 0;
	} else if (batt_pres == BP_YES && batt_pres_prev == BP_NO &&
		   !battery_report_present_timer_started) {
		/*
		 * Wait 1 second before reporting present if it was
		 * previously reported as not present
		 */
		battery_report_present_timer_started = 1;
		battery_report_present = 0;
		hook_call_deferred(&battery_now_present_data, SECOND);
	}

	if (!battery_report_present)
		batt_pres = BP_NO;

	batt_pres_prev = batt_pres;

	return batt_pres;
}

int board_battery_initialized(void)
{
	return battery_hw_present() == batt_pres_prev;
}

static int board_battery_sb_write(uint8_t access, int cmd)
{
	int rv;
	uint8_t buf[1 + sizeof(uint16_t)];

	/*
	 * Note, the i2c_lock must be handled by the calling function. The
	 * battery unseal operation requires two writes without any other access
	 * taking place. Therefore the calling function handles when to
	 * grab/release the lock.
	 */

	buf[0] = access;
	buf[1] = cmd & 0xff;
	buf[2] = (cmd >> 8) & 0xff;

	rv = i2c_xfer(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS, buf,
		      1 + sizeof(uint16_t), NULL, 0);

	return rv;
}

int board_battery_read_mfgacc(int offset, int access, uint8_t *buf, int len)
{
	int rv;
	uint8_t block_len, reg;

	/* start read */
	i2c_lock(I2C_PORT_BATTERY, 1);

	/* Send write block */
	rv = board_battery_sb_write(SB_MANUFACTURER_ACCESS, offset);
	if (rv) {
		i2c_lock(I2C_PORT_BATTERY, 0);
		return rv;
	}

	reg = access;
	rv = i2c_xfer_unlocked(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS, &reg, 1,
			       &block_len, 1, I2C_XFER_START);
	if (rv) {
		i2c_lock(I2C_PORT_BATTERY, 0);
		return rv;
	}

	/* Compare block length to desired read length */
	if (len && (block_len > len))
		block_len = len;

	rv = i2c_xfer_unlocked(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS, NULL, 0,
			       buf, block_len, I2C_XFER_STOP);
	i2c_lock(I2C_PORT_BATTERY, 0);

	return rv;
}

static int board_battery_unseal(uint32_t param)
{
	int rv;
	uint8_t data[6];

	/* Get Operation Status */
	rv = board_battery_read_mfgacc(PARAM_OPERATION_STATUS,
				       SB_ALT_MANUFACTURER_ACCESS, data,
				       sizeof(data));

	if (rv)
		return EC_ERROR_UNKNOWN;

	if ((data[3] & 0x3) == 0x3) {
		/*
		 * Hold the lock for both writes to ensure that no other
		 * manufactuer access opertion can take place.
		 */
		i2c_lock(I2C_PORT_BATTERY, 1);
		rv = board_battery_sb_write(SB_MANUFACTURER_ACCESS,
					    param & 0xffff);
		if (rv)
			goto unseal_fail;

		rv = board_battery_sb_write(SB_MANUFACTURER_ACCESS,
					    (param >> 16) & 0xffff);
		if (rv)
			goto unseal_fail;

		i2c_lock(I2C_PORT_BATTERY, 0);

		/* Verify that battery is unsealed */
		rv = board_battery_read_mfgacc(PARAM_OPERATION_STATUS,
					       SB_ALT_MANUFACTURER_ACCESS, data,
					       sizeof(data));
		if (rv || ((data[3] & 0x3) != 0x2))
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;

unseal_fail:
	i2c_lock(I2C_PORT_BATTERY, 0);
	return EC_RES_ERROR;
}

static int board_battery_seal(void)
{
	int rv;

	i2c_lock(I2C_PORT_BATTERY, 1);
	rv = board_battery_sb_write(SB_MANUFACTURER_ACCESS, 0x0030);
	i2c_lock(I2C_PORT_BATTERY, 0);

	if (rv != EC_SUCCESS)
		return EC_RES_ERROR;

	return EC_SUCCESS;
}

static int board_battery_write_flash(int addr, uint32_t data, int len)
{
	int rv;
	uint8_t buf[sizeof(uint32_t) + 4];

	if (len > 4)
		return EC_ERROR_INVAL;

	buf[0] = SB_ALT_MANUFACTURER_ACCESS;
	/* Number of bytes to write, including the address */
	buf[1] = len + 2;
	/* Put in the flash address */
	buf[2] = addr & 0xff;
	buf[3] = (addr >> 8) & 0xff;

	/* Add data to be written */
	buf[4] = data & 0xff;
	buf[5] = (data >> 8) & 0xff;
	buf[6] = (data >> 16) & 0xff;
	buf[7] = (data >> 24) & 0xff;
	/* Account for command, length, and address */
	len += 4;

	i2c_lock(I2C_PORT_BATTERY, 1);
	rv = i2c_xfer(I2C_PORT_BATTERY, BATTERY_ADDR_FLAGS, buf, len, NULL, 0);
	i2c_lock(I2C_PORT_BATTERY, 0);

	return rv;
}

static int board_battery_read_flash(int block, int len, uint8_t *buf)
{
	uint8_t data[6];
	int rv;
	int i;

	if (len > 4)
		len = 4;
	rv = board_battery_read_mfgacc(block, SB_ALT_MANUFACTURER_ACCESS, data,
				       len + 2);
	if (rv)
		return EC_RES_ERROR;

	for (i = 0; i < len; i++)
		buf[i] = data[i + 2];

	return EC_SUCCESS;
}

static int board_battery_disable_cto(uint32_t value)
{
	uint8_t protect_c;

	if (board_battery_unseal(value))
		return EC_RES_ERROR;

	/* Check CTO enable */
	if (board_battery_read_flash(SB_VENDOR_ENABLED_PROTECT_C, 1,
				     &protect_c)) {
		board_battery_seal();
		return EC_RES_ERROR;
	}

	if (protect_c != EXPECTED_CTO_DISABLE_VALUE) {
		board_battery_write_flash(SB_VENDOR_ENABLED_PROTECT_C,
					  EXPECTED_CTO_DISABLE_VALUE, 1);
		/* After flash write, allow time for it to complete */
		msleep(100);
		/* Read the current protect_c register value */
		if (board_battery_read_flash(SB_VENDOR_ENABLED_PROTECT_C, 1,
					     &protect_c) == EC_SUCCESS)
			protect_c_reg = protect_c;
	} else {
		protect_c_reg = protect_c;
	}

	if (board_battery_seal()) {
		/* If failed, then wait one more time and seal again */
		msleep(100);
		if (board_battery_seal())
			return EC_RES_ERROR;
	}

	return EC_SUCCESS;
}

static int board_battery_fix_otd_recovery_temp(uint32_t value)
{
	int16_t otd_recovery_temp;

	if (board_battery_unseal(value))
		return EC_RES_ERROR;

	/* Check current OTD recovery temp */
	if (board_battery_read_flash(SB_VENDOR_OTD_RECOVERY_TEMP, 2,
				     (uint8_t *)&otd_recovery_temp)) {
		board_battery_seal();
		return EC_RES_ERROR;
	}

	if (otd_recovery_temp != EXPECTED_OTD_RECOVERY_TEMP) {
		board_battery_write_flash(SB_VENDOR_OTD_RECOVERY_TEMP,
					  EXPECTED_OTD_RECOVERY_TEMP, 2);
		/* After flash write, allow time for it to complete */
		msleep(100);
		/* Read the current OTD recovery temperature */
		if (!board_battery_read_flash(SB_VENDOR_OTD_RECOVERY_TEMP, 2,
					      (uint8_t *)&otd_recovery_temp))
			otd_recovery_temp_reg = otd_recovery_temp;
	} else {
		otd_recovery_temp_reg = otd_recovery_temp;
	}

	if (board_battery_seal()) {
		/* If failed, then wait one more time and seal again */
		msleep(100);
		if (board_battery_seal())
			return EC_RES_ERROR;
	}

	return EC_SUCCESS;
}

__override int battery_get_vendor_param(uint32_t param, uint32_t *value)
{
	/*
	 * These registers can't be read directly because the flash area
	 * of the battery is protected, unless it's been
	 * unsealed. The key is only able to be passed in the set
	 * function. The get function is always called following the set
	 * function. Therefore when the set function is called, this
	 * register value is read and saved to protect_c_reg. If this
	 * value is < 0, then the set function wasn't called and
	 * therefore the value can't be known.
	 */
	switch (param) {
	case SB_VENDOR_PARAM_CTO_DISABLE:
		if (protect_c_reg >= 0) {
			*value = protect_c_reg;
			return EC_SUCCESS;
		}
		break;
	case SB_VENDOR_PARAM_OTD_RECOVERY_TEMP:
		if (otd_recovery_temp_reg >= 0) {
			*value = otd_recovery_temp_reg;
			return EC_SUCCESS;
		}
		break;
	default:
		return EC_ERROR_UNIMPLEMENTED;
	}
	return EC_RES_ERROR;
}

__override int battery_set_vendor_param(uint32_t param, uint32_t value)
{
	switch (param) {
	case SB_VENDOR_PARAM_CTO_DISABLE:
		if (board_battery_disable_cto(value))
			return EC_ERROR_UNKNOWN;
		break;
	case SB_VENDOR_PARAM_OTD_RECOVERY_TEMP:
		if (board_battery_fix_otd_recovery_temp(value))
			return EC_ERROR_UNKNOWN;
		break;
	default:
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}
