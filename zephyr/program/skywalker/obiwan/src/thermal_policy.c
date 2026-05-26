/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "console.h"
#include "driver/charger/bq25710.h"
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "hooks.h"
#include "power.h"
#include "temp_sensor/temp_sensor.h"
#include "usb_pd.h"
#include "usbc/pdc_power_mgmt.h"
#include "util.h"

#define POLL_COUNT 3
#define CHARGER_LIMIT_LEVELS 4
#define TYPEC_LIMIT_LEVELS 3
#define TEMP_MAX 120
#define TEMP_MIN 0

#define BATT_2_CELL 2
#define BATT_3_CELL 3
#define BATT_2_CELL_VSYS 6600
#define BATT_3_CELL_VSYS 9200
#define RETRY_CHARGER 3

#define MODEL_L26N2PK1 "l26n2pk1"
#define MODEL_L26B2PK1 "l26b2pk1"
#define MODEL_L26X2PK1 "l26x2pk1"
#define MODEL_L26M2PK1 "l26m2pk1"
#define MODEL_L26D2PK1 "l26d2pk1"

static int current_limit;

static uint8_t charger_limit_level = 0;
static uint8_t charger_trigger_cnt = 0;
static uint8_t charger_release_cnt = 0;

static uint8_t typec_limit_level = 0;
static uint8_t typec_trigger_cnt = 0;
static uint8_t typec_release_cnt = 0;

static uint8_t pre_battery_cells = BATT_3_CELL;
static bool check_charger_state = false;
static uint8_t retry_charger_cyc = 0;

typedef enum {
	LIMIT_NONE = 9999,
	LIMIT_3500 = 3500,
	LIMIT_3000 = 3000,
	LIMIT_2000 = 2000,
	LIMIT_1000 = 1000,
	LIMIT_500 = 500
} charge_limit_t;

typedef struct {
	uint8_t trigger_temp;
	uint8_t release_temp;
} temp_limit_t;

static const charge_limit_t charger_limit_table[CHARGER_LIMIT_LEVELS] = {
	LIMIT_NONE, LIMIT_3500, LIMIT_3000, LIMIT_2000
};

static const charge_limit_t typec_limit_table[TYPEC_LIMIT_LEVELS] = {
	LIMIT_NONE, LIMIT_1000, LIMIT_500
};

static const temp_limit_t charge_temp_limits[CHARGER_LIMIT_LEVELS - 1] = {
	{ 45, 43 },
	{ 49, 45 },
	{ 53, 49 }
};

static const temp_limit_t typec_5v_temp_limits[TYPEC_LIMIT_LEVELS - 1] = {
	{ 63, 60 },
	{ 68, 65 }
};

static const temp_limit_t typec_chg_temp_limits[TYPEC_LIMIT_LEVELS - 1] = {
	{ 60, 58 },
	{ 65, 62 }
};

static bool temp_is_valid(int temp)
{
	return temp > TEMP_MIN && temp < TEMP_MAX;
}

static void update_charge_limit(void)
{
	int charger_temp_k, charger_temp;

	temp_sensor_read(TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_charger)),
			 &charger_temp_k);

	charger_temp = K_TO_C(charger_temp_k);

	if (!temp_is_valid(charger_temp)) {
		return;
	}

	if (charger_limit_level < CHARGER_LIMIT_LEVELS - 1) {
		temp_limit_t chg = charge_temp_limits[charger_limit_level];

		if (charger_temp >= chg.trigger_temp) {
			if (++charger_trigger_cnt >= POLL_COUNT) {
				charger_limit_level++;
				charger_trigger_cnt = 0;
				charger_release_cnt = 0;
			}
		} else {
			charger_trigger_cnt = 0;
		}
	}

	if (charger_limit_level > 0) {
		temp_limit_t chg = charge_temp_limits[charger_limit_level - 1];

		if (charger_temp < chg.release_temp) {
			if (++charger_release_cnt >= POLL_COUNT) {
				charger_limit_level--;
				charger_trigger_cnt = 0;
				charger_release_cnt = 0;
			}
		} else {
			charger_release_cnt = 0;
		}
	}
}

static void update_typec_limit(void)
{
	int typec_5v_temp_k, typec_5v_temp;
	int typec_chg_temp_k, typec_chg_temp;

	temp_sensor_read(TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_5v)),
			 &typec_5v_temp_k);
	temp_sensor_read(TEMP_SENSOR_ID_BY_DEV(DT_NODELABEL(temp_charger)),
			 &typec_chg_temp_k);

	typec_5v_temp = K_TO_C(typec_5v_temp_k);
	typec_chg_temp = K_TO_C(typec_chg_temp_k);

	if (!temp_is_valid(typec_5v_temp) || !temp_is_valid(typec_chg_temp)) {
		return;
	}

	if (typec_limit_level < TYPEC_LIMIT_LEVELS - 1) {
		temp_limit_t typec_5v = typec_5v_temp_limits[typec_limit_level];
		temp_limit_t typec_chg =
			typec_chg_temp_limits[typec_limit_level];

		if (typec_5v_temp >= typec_5v.trigger_temp &&
		    typec_chg_temp >= typec_chg.trigger_temp) {
			if (++typec_trigger_cnt >= POLL_COUNT) {
				typec_limit_level++;
				typec_trigger_cnt = 0;
				typec_release_cnt = 0;
			}
		} else {
			typec_trigger_cnt = 0;
		}
	}

	if (typec_limit_level > 0) {
		temp_limit_t typec_5v =
			typec_5v_temp_limits[typec_limit_level - 1];
		temp_limit_t typec_chg =
			typec_chg_temp_limits[typec_limit_level - 1];

		if (typec_5v_temp < typec_5v.release_temp ||
		    typec_chg_temp < typec_chg.release_temp) {
			if (++typec_release_cnt >= POLL_COUNT) {
				typec_limit_level--;
				typec_trigger_cnt = 0;
				typec_release_cnt = 0;
			}
		} else {
			typec_release_cnt = 0;
		}
	}
}

static void update_current_limit(void)
{
	int i;
	bool any_port_is_source = false;

	if (extpower_is_present() && power_get_state() == POWER_S0) {
		update_charge_limit();

		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			if (pd_get_power_role(i) == PD_ROLE_SOURCE) {
				update_typec_limit();
				any_port_is_source = true;
			}
		}

		if (!any_port_is_source) {
			typec_limit_level = 0;
			typec_trigger_cnt = 0;
			typec_release_cnt = 0;
		}
	} else {
		charger_limit_level = typec_limit_level = 0;
		charger_trigger_cnt = typec_trigger_cnt = 0;
		charger_release_cnt = typec_release_cnt = 0;
	}

	current_limit = min(charger_limit_table[charger_limit_level],
			    typec_limit_table[typec_limit_level]);
}
DECLARE_HOOK(HOOK_SECOND, update_current_limit, HOOK_PRIO_TEMP_SENSOR_DONE);

static inline int min_system_reg_to_voltage_mv(int reg)
{
	int steps;
	steps = GET_BQ_FIELD(BQ25720, VSYS_MIN, VOLTAGE, reg);
	return steps * BQ25720_VSYS_MIN_VOLTAGE_STEP_MV;
}

static bool is_2s_battery(const char *model)
{
	return !strncasecmp(model, MODEL_L26N2PK1, strlen(MODEL_L26N2PK1)) ||
	       !strncasecmp(model, MODEL_L26B2PK1, strlen(MODEL_L26B2PK1)) ||
	       !strncasecmp(model, MODEL_L26X2PK1, strlen(MODEL_L26X2PK1)) ||
	       !strncasecmp(model, MODEL_L26M2PK1, strlen(MODEL_L26M2PK1)) ||
	       !strncasecmp(model, MODEL_L26D2PK1, strlen(MODEL_L26D2PK1));
}

void battery_policy(void)
{
	if (battery_is_present() != BP_YES)
		return;

	struct battery_static_info *bs = &battery_static[BATT_IDX_MAIN];

	if (!bs->model_ext[0]) {
		update_static_battery_info();
	}

	if (!is_2s_battery(bs->model_ext)) {
		/* 3-cell*/
		if (pre_battery_cells != BATT_3_CELL) {
			pre_battery_cells = BATT_3_CELL;
			check_charger_state = true;
			retry_charger_cyc = 0;
		}
	} else {
		/* 2-cell*/
		if (pre_battery_cells != BATT_2_CELL) {
			pre_battery_cells = BATT_2_CELL;
			check_charger_state = true;
			retry_charger_cyc = 0;
		}
	}

	if (check_charger_state == true) {
		if (retry_charger_cyc >= RETRY_CHARGER) {
			check_charger_state = false;
			return;
		}
		int value, vsys;
		bq25710_set_min_system_voltage(
			0, pre_battery_cells == BATT_2_CELL ? BATT_2_CELL_VSYS :
							      BATT_3_CELL_VSYS);
		if (i2c_read16(chg_chips[0].i2c_port,
			       chg_chips[0].i2c_addr_flags,
			       BQ25710_REG_MIN_SYSTEM_VOLTAGE, &value)) {
			ccprints("charger read failed");
			retry_charger_cyc++;
			if (retry_charger_cyc >= RETRY_CHARGER) {
				pdc_power_mgmt_set_comms_state(false);
			}
			return;
		}

		vsys = min_system_reg_to_voltage_mv(value);

		if (pre_battery_cells == BATT_2_CELL &&
		    vsys >= BATT_2_CELL_VSYS && vsys <= BATT_3_CELL_VSYS) {
			check_charger_state = false;
			return;
		} else if (pre_battery_cells == BATT_3_CELL &&
			   vsys >= BATT_3_CELL_VSYS) {
			check_charger_state = false;
			return;
		} else {
			ccprints("charger vsys set %d but read %d",
				 pre_battery_cells == BATT_2_CELL ?
					 BATT_2_CELL_VSYS :
					 BATT_3_CELL_VSYS,
				 vsys);

			retry_charger_cyc++;
			if (retry_charger_cyc >= RETRY_CHARGER) {
				pdc_power_mgmt_set_comms_state(false);
			}
		}
	}
}

void ac_charge_policy(void)
{
	int value, vsys;
	if (i2c_read16(chg_chips[0].i2c_port, chg_chips[0].i2c_addr_flags,
		       BQ25710_REG_MIN_SYSTEM_VOLTAGE, &value)) {
		check_charger_state = true;
		retry_charger_cyc = 0;
		return;
	}

	vsys = min_system_reg_to_voltage_mv(value);

	if (pre_battery_cells == BATT_2_CELL && vsys >= BATT_2_CELL_VSYS &&
	    vsys <= BATT_3_CELL_VSYS)
		return;
	else if (pre_battery_cells == BATT_3_CELL && vsys >= BATT_3_CELL_VSYS)
		return;
	else {
		ccprints("charger vsys set %d but read %d",
			 pre_battery_cells == BATT_2_CELL ? BATT_2_CELL_VSYS :
							    BATT_3_CELL_VSYS,
			 vsys);
		check_charger_state = true;
		retry_charger_cyc = 0;
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_charge_policy, HOOK_PRIO_DEFAULT);

int pd_get_usb_pd_3a_ports(void)
{
	return pre_battery_cells == BATT_2_CELL ? 0 : 1;
}

int charger_profile_override(struct charge_state_data *curr)
{
	curr->requested_current = min(curr->requested_current, current_limit);

	battery_policy();
	return EC_SUCCESS;
}

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
