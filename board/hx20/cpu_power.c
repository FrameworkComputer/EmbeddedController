


#include "charge_state.h"
#include "charger.h"
#include "charge_manager.h"
#include "chipset.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "peci.h"
#include "peci_customization.h"
#include "cypress5525.h"
#include "math_util.h"
#include "util.h"
#include "ec_commands.h"
#include "fan.h"
#include "temp_sensor.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

// Loaded on EC reset
#define POWER_LIMIT_1_W_DEFAULT	40
#define POWER_LIMIT_2_W_DEFAULT	64
#define POWER_LIMIT_4_W_DEFAULT	121

// Loaded by ectool command "cpupower default"
#define POWER_LIMIT_1_W_USER_DEFAULT	28
#define POWER_LIMIT_2_W_USER_DEFAULT	64
#define POWER_LIMIT_4_W_USER_DEFAULT	121

static int POWER_LIMIT_1_W = POWER_LIMIT_1_W_DEFAULT;
static int POWER_LIMIT_2_W = POWER_LIMIT_2_W_DEFAULT;
static int POWER_LIMIT_4_W = POWER_LIMIT_4_W_DEFAULT;

static int pl1_watt = POWER_LIMIT_1_W_DEFAULT;
static int pl2_watt = POWER_LIMIT_2_W_DEFAULT;
static int pl4_watt = POWER_LIMIT_4_W_DEFAULT;
static int psys_watt;
bool manual_ctl;

void set_pl_limits(int pl1, int pl2, int pl4, int psys)
{
		peci_update_PL1(pl1);
		peci_update_PL2(pl2);
		peci_update_PL4(pl4);
		peci_update_PsysPL2(psys);
}

void update_soc_power_limit(bool force_update, bool force_no_adapter)
{
	/*
	 * power limit is related to AC state, battery percentage, and power budget
	 */

	int active_power;
	int pps_power_budget;
	int battery_percent;

	static int old_pl1_watt = -1;
	static int old_pl2_watt = -1;
	static int old_pl4_watt = -1;
	static int old_psys_watt = -1;

	/* TODO: get the power and pps_power_budget */
	battery_percent = charge_get_percent();
	active_power = charge_manager_get_power_limit_uw()/1000000;
	pps_power_budget = cypd_get_pps_power_budget();

	if (force_no_adapter) {
		active_power = 0;
	}

	if (!extpower_is_present() || (active_power < 55)) {
		/* Battery only or ADP < 55W */
		pl2_watt = POWER_LIMIT_1_W;
		pl4_watt = 70 - pps_power_budget;
		psys_watt = 52 - pps_power_budget;
	} else if (battery_percent < 30) {
		/* ADP > 55W and Battery percentage < 30% */
		pl4_watt = active_power - 15 - pps_power_budget;
		pl2_watt = MIN((pl4_watt * 90) / 100, 64);
		psys_watt = ((active_power * 95) / 100) - pps_power_budget;
	} else {
		/* ADP > 55W and Battery percentage >= 30% */
		pl1_watt = POWER_LIMIT_1_W;
		pl2_watt = POWER_LIMIT_2_W;
		pl4_watt = POWER_LIMIT_4_W;
		/* psys watt = adp watt * 0.95 + battery watt(55 W) * 0.7 - pps power budget */
		psys_watt = ((active_power * 95) / 100) + 39 - pps_power_budget;
	}
	if (pl2_watt != old_pl2_watt || pl4_watt != old_pl4_watt ||
			psys_watt != old_psys_watt || force_update || 
			pl1_watt != old_pl1_watt) {
		old_pl1_watt = pl1_watt;
		old_psys_watt = psys_watt;
		old_pl4_watt = pl4_watt;
		old_pl2_watt = pl2_watt;

		CPRINTS("Updating SOC Power Limits: PL1 %d, PL2 %d, PL4 %d, Psys %d, Adapter %d",
			pl1_watt, pl2_watt, pl4_watt, psys_watt, active_power);
		set_pl_limits(pl1_watt, pl2_watt, pl4_watt, psys_watt);
	}
}

void update_soc_power_limit_hook(void)
{
	update_soc_power_limit(false, false);
}

DECLARE_HOOK(HOOK_AC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, update_soc_power_limit_hook, HOOK_PRIO_DEFAULT);



/* Fan mode enumeration */
enum fan_mode {
	FAN_MODE_SILENT = 0,
	FAN_MODE_NORMAL = 1,
	FAN_MODE_EXTREME = 2,
};

static enum fan_mode current_fan_mode = FAN_MODE_NORMAL;

/* Update thermal params based on fan mode */
void update_thermal_params_for_mode(enum fan_mode mode)
{
	extern struct ec_thermal_config thermal_params[TEMP_SENSOR_COUNT];

	if (mode == FAN_MODE_SILENT) {
		thermal_params[TEMP_SENSOR_CPU].temp_fan_off = C_TO_K(50);
		thermal_params[TEMP_SENSOR_CPU].temp_fan_max = C_TO_K(85);
	} else if (mode == FAN_MODE_EXTREME) {
		thermal_params[TEMP_SENSOR_CPU].temp_fan_off = C_TO_K(20);
		thermal_params[TEMP_SENSOR_CPU].temp_fan_max = C_TO_K(58);
	} else {
		/* NORMAL mode (default) */
		thermal_params[TEMP_SENSOR_CPU].temp_fan_off = C_TO_K(40);
		thermal_params[TEMP_SENSOR_CPU].temp_fan_max = C_TO_K(69);
	}
	current_fan_mode = mode;
	fan_set_thermal_control_enabled(0, 1); /* Re-enable thermal control to apply new settings */
}

/* Fan mode host command handler */
static enum ec_status host_command_fan_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_fan_mode *p = args->params;
	struct ec_response_fan_mode *r = args->response;

	/* If params provided, set the mode */
	if (args->params_size > 0 && p) {
		if (p->mode <= FAN_MODE_EXTREME) {
			update_thermal_params_for_mode(p->mode);
			CPRINTS("Fan mode set to %d", p->mode);
		}
	}

	/* Return current mode */
	r->mode = current_fan_mode;
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_FAN_MODE, host_command_fan_mode, EC_VER_MASK(0));

/* Console command for fan mode */
static int cmd_fan_mode(int argc, char **argv)
{
	const char *mode_names[] = {"silent", "normal", "extreme"};
	int mode;

	if (argc > 1) {
		if (!strcasecmp(argv[1], "silent"))
			mode = FAN_MODE_SILENT;
		else if (!strcasecmp(argv[1], "normal"))
			mode = FAN_MODE_NORMAL;
		else if (!strcasecmp(argv[1], "extreme"))
			mode = FAN_MODE_EXTREME;
		else {
			CPRINTS("Invalid mode. Use: silent, normal, or extreme");
			return EC_ERROR_PARAM1;
		}
		update_thermal_params_for_mode(mode);
	}

	CPRINTS("Current fan mode: %s", mode_names[current_fan_mode]);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(fanmode, cmd_fan_mode,
			"[silent|normal|extreme]",
			"Set/Get fan mode");

/* Host command handler for CPU power limits */
static enum ec_status host_command_cpu_power(struct host_cmd_handler_args *args)
{
	const struct ec_params_cpu_power *p = args->params;
	struct ec_response_cpu_power *r = args->response;

	/* If parameters provided, set the values */
	if (args->params_size > 0 && p) {
		/*
		 * "default" from ectool is encoded as an all-zero payload.
		 */
		if (p->pl1_mW == 0 && p->pl2_mW == 0 && p->pl4_mW == 0) {
			POWER_LIMIT_1_W = POWER_LIMIT_1_W_USER_DEFAULT;
			POWER_LIMIT_2_W = POWER_LIMIT_2_W_USER_DEFAULT;
			POWER_LIMIT_4_W = POWER_LIMIT_4_W_USER_DEFAULT;
		} else {
			if (p->pl1_mW != 0)
				POWER_LIMIT_1_W = p->pl1_mW / 1000;
			if (p->pl2_mW != 0)
				POWER_LIMIT_2_W = p->pl2_mW / 1000;
			if (p->pl4_mW != 0)
				POWER_LIMIT_4_W = p->pl4_mW / 1000;
		}
		update_soc_power_limit(true, false);
	}

	/* Return current power limits in mW */
	r->pl1_mW = pl1_watt * 1000;
	r->pl2_mW = pl2_watt * 1000;
	r->pl4_mW = pl4_watt * 1000;
	r->psys_mW = psys_watt * 1000;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_CPU_POWER, host_command_cpu_power, EC_VER_MASK(0));