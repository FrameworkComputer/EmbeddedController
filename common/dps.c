/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dynamic PDO Selection.
 */

#include "atomic.h"
#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "dps.h"
#include "ec_commands.h"
#include "hooks.h"
#include "math_util.h"
#include "task.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#include <stdint.h>

#define K_MORE_PWR 96
#define K_LESS_PWR 93
#define K_SAMPLE 1
#define K_WINDOW 3
#define T_REQUEST_STABLE_TIME (10 * SECOND)
#define T_NEXT_CHECK_TIME (5 * SECOND)

#define DPS_FLAG_STOP_EVENTS \
	(DPS_FLAG_DISABLED | DPS_FLAG_NO_SRCCAP | DPS_FLAG_NO_BATTERY)
#define DPS_FLAG_ALL GENMASK(31, 0)

#define MAX_MOVING_AVG_WINDOW 5

BUILD_ASSERT(K_MORE_PWR > K_LESS_PWR && 100 >= K_MORE_PWR && 100 >= K_LESS_PWR);

/* lock for updating timeout value */
static K_MUTEX_DEFINE(dps_lock);
static timestamp_t timeout;
static bool is_enabled = true;
static int debug_level;
static bool fake_enabled;
static int fake_mv, fake_ma;
static int dynamic_mv;
static int dps_port = CHARGE_PORT_NONE;
static atomic_t flag;

#define CPRINTF(format, args...) cprintf(CC_USBPD, "DPS " format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, "DPS " format, ##args)

__overridable struct dps_config_t dps_config = {
	.k_less_pwr = K_LESS_PWR,
	.k_more_pwr = K_MORE_PWR,
	.k_sample = K_SAMPLE,
	.k_window = K_WINDOW,
	.t_stable = T_REQUEST_STABLE_TIME,
	.t_check = T_NEXT_CHECK_TIME,
	.is_more_efficient = NULL,
};

__test_only struct dps_config_t *dps_get_config(void)
{
	return &dps_config;
}

int dps_get_dynamic_voltage(void)
{
	return dynamic_mv;
}

int dps_get_charge_port(void)
{
	return dps_port;
}

bool dps_is_enabled(void)
{
	return is_enabled;
}

test_export_static void dps_enable(bool en)
{
	bool prev_en = is_enabled;

	is_enabled = en;

	if (is_enabled && !prev_en) {
		task_wake(TASK_ID_DPS);
	}

	if (!is_enabled) {
		/* issue a new PD request for a default voltage */
		if (dps_port != CHARGE_PORT_NONE)
			pd_dpm_request(dps_port, DPM_REQUEST_NEW_POWER_LEVEL);
	}
}

static void update_timeout(int us)
{
	timestamp_t new_timeout;

	new_timeout.val = get_time().val + us;

	mutex_lock(&dps_lock);
	if (new_timeout.val > timeout.val)
		timeout = new_timeout;
	mutex_unlock(&dps_lock);
}

/*
 * DPS reset.
 */
static void dps_reset(void)
{
	dynamic_mv = PD_MAX_VOLTAGE_MV;
	dps_port = CHARGE_PORT_NONE;
}

/*
 * DPS initialization.
 */
test_export_static int dps_init(void)
{
	int rc = EC_SUCCESS;

	dps_reset();

	if (dps_config.k_window > MAX_MOVING_AVG_WINDOW) {
		CPRINTS("ERR:WIN");
		rc = EC_ERROR_INVALID_CONFIG;
	}

	if (dps_config.k_less_pwr > 100 || dps_config.k_more_pwr > 100 ||
	    dps_config.k_more_pwr <= dps_config.k_less_pwr) {
		CPRINTS("ERR:COEF");
		rc = EC_ERROR_INVALID_CONFIG;
	}

	return rc;
}

static bool is_near_limit(int val, int limit)
{
	return val >= (limit * dps_config.k_more_pwr / 100);
}

bool is_more_efficient(int curr_mv, int prev_mv, int batt_mv, int batt_mw,
		       int input_mw)
{
	if (dps_config.is_more_efficient)
		return dps_config.is_more_efficient(curr_mv, prev_mv, batt_mv,
						    batt_mw, input_mw);

	return ABS(curr_mv - batt_mv) < ABS(prev_mv - batt_mv);
}

/*
 * Get the input power of the active port.
 *
 * input_power = vbus * input_current
 *
 * @param vbus: VBUS in mV
 * @param input_curr: input current in mA
 *
 * @return input_power of the result of vbus * input_curr in mW
 */
test_mockable_static int get_desired_input_power(int *vbus, int *input_current)
{
	int active_port;
	int charger_id;
	enum ec_error_list rv;

	active_port = charge_manager_get_active_charge_port();

	if (active_port == CHARGE_PORT_NONE)
		return 0;

	charger_id = charge_get_active_chg_chip();

	if (fake_enabled) {
		*vbus = fake_mv;
		*input_current = fake_ma;
		return fake_mv * fake_ma / 1000;
	}

	rv = charger_get_input_current(charger_id, input_current);
	if (rv)
		return 0;

	*vbus = charge_manager_get_vbus_voltage(active_port);

	return (*vbus) * (*input_current) / 1000;
}

test_mockable_static int get_battery_target_voltage(int *target_mv)
{
	int charger_id = charge_get_active_chg_chip();
	int error = charger_get_voltage(charger_id, target_mv);

	if (!error) {
		return EC_SUCCESS;
	}
	if (error != EC_ERROR_UNIMPLEMENTED) {
		CPRINTS("Failed to get voltage for charge port %d: %d",
			charger_id, error);
		return error;
	}
	/*
	 * Fall back to battery design voltage if charger output voltage
	 * is not available.
	 */
	return battery_design_voltage(target_mv);
}

/*
 * Get the most efficient PDO voltage for the battery of the charging port
 *
 * | W\Batt | 1S(3.7V) | 2S(7.4V) | 3S(11.1V) | 4S(14.8V) |
 * --------------------------------------------------------
 * | 0-15W  | 5V       | 9V       | 12V       | 15V       |
 * | 15-27W | 9V       | 9V       | 12V       | 15V       |
 * | 27-36W | 12V      | 12V      | 12V       | 15V       |
 * | 36-45W | 15V      | 15V      | 15V       | 15V       |
 * | 45-60W | 20V      | 20V      | 20V       | 20V       |
 *
 *
 * @return 0 if error occurs, else battery efficient voltage in mV
 */
test_mockable_static int get_efficient_voltage(void)
{
	int eff_mv = 0;
	int batt_mv;
	int batt_pwr;
	int input_pwr, vbus, input_curr;
	const struct batt_params *batt = charger_current_battery_params();

	input_pwr = get_desired_input_power(&vbus, &input_curr);

	if (!input_pwr)
		return 0;

	if (get_battery_target_voltage(&batt_mv))
		return 0;

	batt_pwr = batt->current * batt->voltage / 1000;

	for (int i = 0; i < board_get_usb_pd_port_count(); ++i) {
		const int cnt = pd_get_src_cap_cnt(i);
		const uint32_t *src_caps = pd_get_src_caps(i);

		for (int j = 0; j < cnt; ++j) {
			int ma, mv, unused;

			pd_extract_pdo_power(src_caps[j], &ma, &mv, &unused);
			/*
			 * If the eff_mv is not picked, or we have more
			 * efficient voltage (less voltage diff)
			 */
			if (eff_mv == 0 ||
			    is_more_efficient(mv, eff_mv, batt_mv, batt_pwr,
					      input_pwr))
				eff_mv = mv;
		}
	}

	return eff_mv;
}

struct pdo_candidate {
	int port;
	int mv;
	int mw;
};

#define UPDATE_CANDIDATE(new_port, new_mv, new_mw) \
	do {                                       \
		cand->port = new_port;             \
		cand->mv = new_mv;                 \
		cand->mw = new_mw;                 \
	} while (0)

#define CLEAR_AND_RETURN()            \
	do {                          \
		moving_avg_count = 0; \
		return false;         \
	} while (0)

test_mockable_static int get_batt_charge_power(void)
{
	const struct batt_params *batt = charger_current_battery_params();

	return batt->current * batt->voltage / 1000;
}

/*
 * Evaluate the system power if a new PD power request is needed.
 *
 * @param struct pdo_candidate: The candidate PDO. (Return value)
 * @return true if a new power request, or false otherwise.
 */
test_mockable_static bool has_new_power_request(struct pdo_candidate *cand)
{
	int vbus, input_curr, input_pwr;
	int input_pwr_avg = 0, input_curr_avg = 0;
	int batt_pwr, batt_mv;
	int max_mv = pd_get_max_voltage();
	int req_pwr, req_ma, req_mv;
	int input_curr_limit;
	int active_port = charge_manager_get_active_charge_port();
	int charger_id;
	static int input_pwrs[MAX_MOVING_AVG_WINDOW];
	static int input_currs[MAX_MOVING_AVG_WINDOW];
	static int prev_active_port = CHARGE_PORT_NONE;
	static int prev_req_mv;
	static int moving_avg_count;

	/* set a default value in case it early returns. */
	UPDATE_CANDIDATE(CHARGE_PORT_NONE, INT32_MAX, 0);

	if (active_port == CHARGE_PORT_NONE)
		CLEAR_AND_RETURN();

	req_mv = pd_get_requested_voltage(active_port);
	req_ma = pd_get_requested_current(active_port);

	if (!req_mv)
		CLEAR_AND_RETURN();

	if (get_battery_target_voltage(&batt_mv))
		CLEAR_AND_RETURN();

	/* if last sample is not the same as the current one, reset counting. */
	if (prev_req_mv != req_mv || prev_active_port != active_port)
		moving_avg_count = 0;
	prev_active_port = active_port;
	prev_req_mv = req_mv;

	req_pwr = req_mv * req_ma / 1000;
	batt_pwr = get_batt_charge_power();
	input_pwr = get_desired_input_power(&vbus, &input_curr);

	if (!input_pwr)
		CLEAR_AND_RETURN();

	/* record moving average */
	input_pwrs[moving_avg_count % dps_config.k_window] = input_pwr;
	input_currs[moving_avg_count % dps_config.k_window] = input_curr;
	if (++moving_avg_count < dps_config.k_window)
		return false;

	for (int i = 0; i < dps_config.k_window; i++) {
		input_curr_avg += input_currs[i];
		input_pwr_avg += input_pwrs[i];
	}
	input_curr_avg /= dps_config.k_window;
	input_pwr_avg /= dps_config.k_window;

	charger_id = charge_get_active_chg_chip();

	if (!charger_get_input_current_limit(charger_id, &input_curr_limit))
		/* set as last requested mA if we're unable to get the limit. */
		input_curr_limit = req_ma;

	/*
	 * input power might be insufficient, force it to negotiate a more
	 * powerful PDO.
	 */
	if (is_near_limit(input_pwr_avg, req_pwr) ||
	    is_near_limit(input_curr_avg, MIN(req_ma, input_curr_limit))) {
		atomic_or(&flag, DPS_FLAG_NEED_MORE_PWR);
		if (!fake_enabled)
			input_pwr_avg = req_pwr + 1;
	} else {
		atomic_clear_bits(&flag, DPS_FLAG_NEED_MORE_PWR);
	}

	if (debug_level)
		CPRINTS("C%d 0x%x last (%dmW %dmV) input (%dmW %dmV %dmA) "
			"avg (%dmW, %dmA)",
			active_port, (int)flag, req_pwr, req_mv, input_pwr,
			vbus, input_curr, input_pwr_avg, input_curr_avg);

	for (int i = 0; i < board_get_usb_pd_port_count(); ++i) {
		const uint32_t *const src_caps = pd_get_src_caps(i);

		/* If the port is not SNK, skip evaluating this port. */
		if (pd_get_power_role(i) != PD_ROLE_SINK)
			continue;

		for (int j = 0; j < pd_get_src_cap_cnt(i); ++j) {
			int ma, mv, unused;
			int mw;
			bool efficient;

			/* TODO(b:169532537): support augmented PDO. */
			if ((src_caps[j] & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
				continue;

			pd_extract_pdo_power(src_caps[j], &ma, &mv, &unused);

			if (mv > max_mv)
				continue;

			mw = MIN(ma, PD_MAX_CURRENT_MA) * mv / 1000;
			efficient = is_more_efficient(mv, cand->mv, batt_mv,
						      batt_pwr, input_pwr_avg);

			if (flag & DPS_FLAG_NEED_MORE_PWR) {
				/* the insufficient case.*/
				if (input_pwr_avg > cand->mw &&
				    (mw > cand->mw ||
				     (mw == cand->mw && efficient))) {
					UPDATE_CANDIDATE(i, mv, mw);
				} else if (input_pwr_avg <= mw && efficient) {
					UPDATE_CANDIDATE(i, mv, mw);
				}
			} else {
				int adjust_pwr =
					mw * dps_config.k_less_pwr / 100;
				int adjust_cand_mw =
					cand->mw * dps_config.k_less_pwr / 100;

				/* Pick if we don't have a candidate yet. */
				if (!cand->mw) {
					UPDATE_CANDIDATE(i, mv, mw);
					/*
					 * if the candidate is insufficient, and
					 * we get one provides more.
					 */
				} else if ((adjust_cand_mw < input_pwr_avg &&
					    cand->mw < mw) ||
					   /*
					    * if the candidate is sufficient,
					    * and we pick a more efficient one.
					    */
					   (adjust_cand_mw >= input_pwr_avg &&
					    adjust_pwr >= input_pwr_avg &&
					    efficient)) {
					UPDATE_CANDIDATE(i, mv, mw);
				}
			}

			/*
			 * if the candidate is the same as the current one, pick
			 * the one at active charge port.
			 */
			if (mw == cand->mw && mv == cand->mv &&
			    i == active_port)
				UPDATE_CANDIDATE(i, mv, mw);
		}
	}

	if (!cand->mv)
		CPRINTS("ERR:CNDMV");

	return (cand->mv != req_mv);
}

__maybe_unused static bool has_srccap(void)
{
	for (int i = 0; i < board_get_usb_pd_port_count(); ++i) {
		if (pd_is_connected(i) &&
		    pd_get_power_role(i) == PD_ROLE_SINK &&
		    pd_get_src_cap_cnt(i) > 0)
			return true;
	}
	return false;
}

void dps_update_stabilized_time(int port)
{
	update_timeout(dps_config.t_stable);
}

void dps_task(void *u)
{
	struct pdo_candidate last_cand = { CHARGE_PORT_NONE, 0, 0 };
	int sample_count = 0;
	int rv;

	rv = dps_init();
	if (rv) {
		CPRINTS("ERR:INIT%d", rv);
		return;
	}

	update_timeout(dps_config.t_check);

	while (1) {
		struct pdo_candidate curr_cand = { CHARGE_PORT_NONE, 0, 0 };
		timestamp_t now;

		now = get_time();
		if (flag & DPS_FLAG_STOP_EVENTS) {
			dps_reset();
			task_wait_event(-1);
			/* clear flags after wake up. */
			atomic_clear(&flag);
			update_timeout(dps_config.t_check);
			continue;
		} else if (now.val < timeout.val) {
			atomic_or(&flag, DPS_FLAG_WAITING);
			task_wait_event(timeout.val - now.val);
			atomic_clear_bits(&flag, DPS_FLAG_WAITING);
			continue;
		}

		if (!is_enabled) {
			atomic_or(&flag, DPS_FLAG_DISABLED);
			continue;
		}

		if (!has_srccap()) {
			atomic_or(&flag, DPS_FLAG_NO_SRCCAP);
			continue;
		}

		if (battery_is_present() != BP_YES) {
			atomic_or(&flag, DPS_FLAG_NO_BATTERY);
			continue;
		}

		if (!has_new_power_request(&curr_cand)) {
			sample_count = 0;
			atomic_clear_bits(&flag, DPS_FLAG_SAMPLED);
		} else {
			if (last_cand.port == curr_cand.port &&
			    last_cand.mv == curr_cand.mv &&
			    last_cand.mw == curr_cand.mw)
				sample_count++;
			else
				sample_count = 1;
			atomic_or(&flag, DPS_FLAG_SAMPLED);
		}

		if (sample_count == dps_config.k_sample) {
			dynamic_mv = curr_cand.mv;
			dps_port = curr_cand.port;
			pd_dpm_request(dps_port, DPM_REQUEST_NEW_POWER_LEVEL);
			sample_count = 0;
			atomic_clear_bits(&flag, (DPS_FLAG_SAMPLED |
						  DPS_FLAG_NEED_MORE_PWR));
		}

		last_cand.port = curr_cand.port;
		last_cand.mv = curr_cand.mv;
		last_cand.mw = curr_cand.mw;
		update_timeout(dps_config.t_check);
	}
}

void check_battery_present(void)
{
	const struct batt_params *batt = charger_current_battery_params();

	if (batt->is_present == BP_YES && (flag & DPS_FLAG_NO_BATTERY)) {
		atomic_clear_bits(&flag, DPS_FLAG_NO_BATTERY);
		task_wake(TASK_ID_DPS);
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, check_battery_present, HOOK_PRIO_DEFAULT);

static int command_dps(int argc, const char **argv)
{
	int port = charge_manager_get_active_charge_port();
	int input_pwr, vbus, input_curr;
	int holder;

	if (argc == 1) {
		uint32_t last_ma = 0, last_mv = 0;
		int batt_mv;

		ccprintf("flag=0x%x k_more=%d k_less=%d k_sample=%d k_win=%d\n",
			 (int)flag, dps_config.k_more_pwr,
			 dps_config.k_less_pwr, dps_config.k_sample,
			 dps_config.k_window);
		ccprintf("t_stable=%d t_check=%d\n",
			 dps_config.t_stable / SECOND,
			 dps_config.t_check / SECOND);
		if (!is_enabled) {
			ccprintf("DPS Disabled\n");
			return EC_SUCCESS;
		}

		if (port == CHARGE_PORT_NONE) {
			ccprintf("No charger attached\n");
			return EC_SUCCESS;
		}

		get_battery_target_voltage(&batt_mv);
		input_pwr = get_desired_input_power(&vbus, &input_curr);
		if (!(flag & DPS_FLAG_NO_SRCCAP)) {
			last_mv = pd_get_requested_voltage(port);
			last_ma = pd_get_requested_current(port);
		}
		ccprintf("C%d DPS Enabled\n"
			 "Requested: %dmV/%dmA\n"
			 "Measured:  %dmV/%dmA/%dmW\n"
			 "Efficient: %dmV\n"
			 "Batt:      %dmv\n"
			 "PDMaxMV:   %dmV\n",
			 port, last_mv, last_ma, vbus, input_curr, input_pwr,
			 get_efficient_voltage(), batt_mv,
			 pd_get_max_voltage());
		return EC_SUCCESS;
	}

	if (!strcasecmp(argv[1], "en")) {
		dps_enable(true);
		return EC_SUCCESS;
	} else if (!strcasecmp(argv[1], "dis")) {
		dps_enable(false);
		return EC_SUCCESS;
	} else if (!strcasecmp(argv[1], "fakepwr")) {
		if (argc == 2) {
			ccprintf("%sabled %dmV/%dmA\n",
				 fake_enabled ? "en" : "dis", fake_mv, fake_ma);
			return EC_SUCCESS;
		}

		if (!strcasecmp(argv[2], "dis")) {
			fake_enabled = false;
			return EC_SUCCESS;
		}

		if (argc < 4)
			return EC_ERROR_PARAM_COUNT;

		holder = atoi(argv[2]);
		if (holder <= 0)
			return EC_ERROR_PARAM2;
		fake_mv = holder;

		holder = atoi(argv[3]);
		if (holder <= 0)
			return EC_ERROR_PARAM3;
		fake_ma = holder;

		fake_enabled = true;
		return EC_SUCCESS;
	}

	if (argc != 3)
		return EC_ERROR_PARAM2;

	if (!strcasecmp(argv[1], "debug")) {
		debug_level = atoi(argv[2]);
	} else if (!strcasecmp(argv[1], "setkmore")) {
		holder = atoi(argv[2]);
		if (holder > 100 || holder <= 0 ||
		    holder < dps_config.k_less_pwr)
			return EC_ERROR_PARAM2;
		dps_config.k_more_pwr = holder;
	} else if (!strcasecmp(argv[1], "setkless")) {
		holder = atoi(argv[2]);
		if (holder > 100 || holder <= 0 ||
		    holder > dps_config.k_more_pwr)
			return EC_ERROR_PARAM2;
		dps_config.k_less_pwr = holder;
	} else if (!strcasecmp(argv[1], "setksample")) {
		holder = atoi(argv[2]);
		if (holder <= 0)
			return EC_ERROR_PARAM2;
		dps_config.k_sample = holder;
	} else if (!strcasecmp(argv[1], "setkwin")) {
		holder = atoi(argv[2]);
		if (holder <= 0 || holder > MAX_MOVING_AVG_WINDOW)
			return EC_ERROR_PARAM2;
		dps_config.k_window = holder;
	} else if (!strcasecmp(argv[1], "settcheck")) {
		holder = atoi(argv[2]);
		if (holder <= 0)
			return EC_ERROR_PARAM2;
		dps_config.t_check = holder * SECOND;
	} else if (!strcasecmp(argv[1], "settstable")) {
		holder = atoi(argv[2]);
		if (holder <= 0)
			return EC_ERROR_PARAM2;
		dps_config.t_stable = holder * SECOND;
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dps, command_dps,
			"en|dis|debug <int>\n"
			"\t\t set(kmore|kless|ksample|kwindow) <int>\n"
			"\t\t set(tstable|tcheck) <int>\n"
			"\t\t fakepwr [dis|<mV> <mA>]",
			"Print/set Dynamic PDO Selection state.");

static enum ec_status hc_usb_pd_dps_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_dps_control *p = args->params;

	dps_enable(p->enable);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_DPS_CONTROL, hc_usb_pd_dps_control,
		     EC_VER_MASK(0));

#ifdef TEST_BUILD
__test_only bool dps_is_fake_enabled(void)
{
	return fake_enabled;
}
__test_only int dps_get_fake_mv(void)
{
	return fake_mv;
}
__test_only int dps_get_fake_ma(void)
{
	return fake_ma;
}
__test_only int *dps_get_debug_level(void)
{
	return &debug_level;
}
__test_only int dps_get_flag(void)
{
	return flag;
}
#endif /* TEST_BUILD */
