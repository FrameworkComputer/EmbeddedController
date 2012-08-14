/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TPS65090 PMU charging task.
 */

#include "board.h"
#include "clock.h"
#include "chipset.h"
#include "console.h"
#include "hooks.h"
#include "gpio.h"
#include "pmu_tpschrome.h"
#include "smart_battery.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

/* Charging and discharging alarms */
#define ALARM_DISCHARGING (ALARM_TERMINATE_DISCHARGE | ALARM_OVER_TEMP)
#define ALARM_CHARGING (ALARM_TERMINATE_CHARGE | \
		ALARM_OVER_CHARGED | \
		ALARM_OVER_TEMP)

/* Maximum retry count to revive a extremely low charge battery */
#define PRE_CHARGING_RETRY 3

/* Time delay in usec for idle, charging and discharging.
 * Defined in battery charging flow.
 */
#define SECOND          (1000 * 1000)
#define T1_OFF_USEC     (60 * SECOND)
#define T1_SUSPEND_USEC (60 * SECOND)
#define T1_USEC         (5  * SECOND)
#define T2_USEC         (10 * SECOND)
#define T3_USEC         (10 * SECOND)

/* Non-SBS charging states */
enum charging_state {
	ST_NONE = 0,
	ST_IDLE,
	ST_PRE_CHARGING,
	ST_CHARGING,
	ST_DISCHARGING,
};

static const char * const state_list[] = {
	"none",
	"idle",
	"pre-charging",
	"charging",
	"discharging"
};

static void enable_charging(int enable)
{
	enable = enable ? 1 : 0;
	if (gpio_get_level(GPIO_CHARGER_EN) != enable)
		gpio_set_level(GPIO_CHARGER_EN, enable);
}

/*
 * TODO(rongchang): move battery vendor specific functions to battery pack
 * module
 */
static int battery_temperature_celsius(int t)
{
	return (t - 2731) / 10;
}

static int battery_start_charging_range(int t)
{
	t = battery_temperature_celsius(t);
	return (t > 5 && t < 40);
}

static int battery_charging_range(int t)
{
	t = battery_temperature_celsius(t);
	return (t > 5 && t < 60);
}

static int battery_discharging_range(int t)
{
	t = battery_temperature_celsius(t);
	return (t < 70);
}

/*
 * Turn off the host application processor
 */
static int system_off(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		CPUTS("[pmu] turn system off\n");
		/* TODO(rongchang): need chipset_force_hard_off(),
		 * and remove these gpio hack
		 */
		gpio_set_level(GPIO_EN_PP3300, 0);
		gpio_set_level(GPIO_EN_PP1350, 0);
		gpio_set_level(GPIO_PMIC_PWRON_L, 1);
		gpio_set_level(GPIO_EN_PP5000, 0);
	}

	return ST_IDLE;
}

/*
 * Notify the host when battery remaining charge is lower than 10%
 */
static int notify_battery_low(void)
{
	static timestamp_t last_notify_time;
	timestamp_t now;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		now = get_time();
		if (now.val - last_notify_time.val > 60000000) {
			CPUTS("[pmu] notify battery low (< 10%)\n");
			last_notify_time = now;
			/* TODO(rongchang): notify AP ? */
		}
	}
	return ST_DISCHARGING;
}

static int config_low_current_charging(int charge)
{
	/* Disable low current termination */
	if (charge < 40)
		return pmu_low_current_charging(1);

	/* Enable low current termination */
	if (charge > 60)
		return pmu_low_current_charging(0);

	return EC_SUCCESS;
}

static int calc_next_state(int state)
{
	int batt_temp, alarm, capacity, charge;

	switch (state) {
	case ST_IDLE:
		/* Check AC and chiset state */
		if (!pmu_get_ac()) {
			if (chipset_in_state(CHIPSET_STATE_ON))
				return ST_DISCHARGING;
			return ST_IDLE;
		}

		/* Enable charging when battery doesn't respond */
		if (battery_temperature(&batt_temp)) {
			if (config_low_current_charging(0))
				return ST_IDLE;
			return ST_PRE_CHARGING;
		}

		/* Turn off charger when battery temperature is out
		 * of the start charging range.
		 */
		if (!battery_start_charging_range(batt_temp))
			return ST_IDLE;

		/* Turn off charger on battery charging alarm */
		if (battery_status(&alarm) || (alarm & ALARM_CHARGING))
			return ST_IDLE;

		/* Start charging only when battery charge lower than 100% */
		if (!battery_state_of_charge(&charge)) {
			config_low_current_charging(charge);
			if (charge < 100)
				return ST_CHARGING;
		}

		return ST_IDLE;

	case ST_PRE_CHARGING:
		if (!pmu_get_ac())
			return ST_IDLE;

		/* If the battery goes online after enable the charger,
		 * go into charging state.
		 */
		if (battery_temperature(&batt_temp) == EC_SUCCESS) {
			if (!battery_start_charging_range(batt_temp))
				return ST_IDLE;
			if (!battery_state_of_charge(&charge)) {
				config_low_current_charging(charge);
				if (charge >= 100)
					return ST_IDLE;
			}
			return ST_CHARGING;
		}

		return ST_PRE_CHARGING;

	case ST_CHARGING:
		/* Go back to idle state when AC is unplugged */
		if (!pmu_get_ac())
			return ST_IDLE;

		/*
		 * Disable charging on battery access error, or charging
		 * temperature out of range.
		 */
		if (battery_temperature(&batt_temp)) {
			CPUTS("[pmu] charging: unable to get battery "
			      "temperature\n");
			return ST_IDLE;
		} else if (!battery_charging_range(batt_temp)) {
			CPRINTF("[pmu] charging: temperature out of range "
				"%dC\n",
				battery_temperature_celsius(batt_temp));
			return ST_IDLE;
		}

		/*
		 * Disable charging on battery alarm events or access error:
		 *   - over temperature
		 *   - over current
		 */
		if (battery_status(&alarm) || (alarm & ALARM_CHARGING)) {
			CPUTS("[pmu] charging: battery alarm\n");
			return ST_IDLE;
		}

		/*
		 * Disable charging on charger alarm events:
		 *   - charger over current
		 *   - charger over temperature
		 */
		if (pmu_is_charger_alarm()) {
			CPUTS("[pmu] charging: charger alarm\n");
			return ST_IDLE;
		}

		return ST_CHARGING;

	case ST_DISCHARGING:
		/* Go back to idle state when AC is plugged */
		if (pmu_get_ac())
			return ST_IDLE;

		/* Prepare EC sleep after system stopped discharging */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			return ST_IDLE;

		/* Check battery discharging temperature range */
		if (battery_temperature(&batt_temp) == 0) {
			if (!battery_discharging_range(batt_temp)) {
				CPRINTF("[pmu] discharging: temperature out of"
					"range %dC\n",
					battery_temperature_celsius(batt_temp));
				return system_off();
			}
		}
		/* Check discharging alarm */
		if (!battery_status(&alarm) && (alarm & ALARM_DISCHARGING)) {
			CPRINTF("[pmu] discharging: battery alarm %016b\n",
					alarm);
			return system_off();
		}
		/* Check remaining charge % */
		if (battery_state_of_charge(&capacity) == 0) {
			if (capacity < 3) {
				system_off();
				return ST_IDLE;
			} else if (capacity < 10) {
				notify_battery_low();
			}
		}

		return ST_DISCHARGING;
	}

	return ST_IDLE;
}

void pmu_charger_task(void)
{
	int state = ST_IDLE;
	int next_state;
	int event = 0;
	int wait_time = T1_USEC;
	unsigned int pre_charging_count = 0;

	pmu_init();
	/*
	 * EC STOP mode support
	 *   The charging loop can be stopped in idle state with AC unplugged.
	 *   Charging loop will be resumed by TPSCHROME interrupt.
	 */
	enable_charging(0);
	disable_sleep(SLEEP_MASK_CHARGING);

	while (1) {
		pmu_clear_irq();

		/*
		 * When battery is extremely low, the internal voltage can not
		 * power on its gas guage IC. Charging loop will enable the
		 * charger and turn on trickle charging. For safty reason,
		 * charger should be disabled if the communication to battery
		 * failed.
		 */
		next_state = pre_charging_count > PRE_CHARGING_RETRY ?
			calc_next_state(ST_IDLE) :
			calc_next_state(state);

		if (next_state != state) {
			pre_charging_count = 0;
			CPRINTF("[batt] state %s -> %s\n",
				state_list[state],
				state_list[next_state]);
			state = next_state;
			if (state == ST_PRE_CHARGING || state == ST_CHARGING)
				enable_charging(1);
			else
				enable_charging(0);
		}

		switch (state) {
		case ST_CHARGING:
			wait_time = T2_USEC;
			break;
		case ST_DISCHARGING:
			wait_time = T3_USEC;
			break;
		case ST_PRE_CHARGING:
			wait_time = T1_USEC;
			if (pre_charging_count > PRE_CHARGING_RETRY)
				enable_charging(0);
			else
				pre_charging_count++;
			break;
		default:
			if (pmu_get_ac()) {
				wait_time = T1_USEC;
				break;
			} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
				wait_time = T1_OFF_USEC;
				enable_sleep(SLEEP_MASK_CHARGING);
			} else if (chipset_in_state(CHIPSET_STATE_SUSPEND)) {
				wait_time = T1_SUSPEND_USEC;
			} else {
				wait_time = T1_USEC;
			}
		}

		/*
		 * Throttle the charging loop. If previous loop was waked up
		 * by an event, sleep 0.5 seconds instead of wait for next
		 * event.
		 */
		if (event & TASK_EVENT_WAKE) {
			usleep(0.5 * SECOND);
			event = 0;
		} else {
			event = task_wait_event(wait_time);
			disable_sleep(SLEEP_MASK_CHARGING);
		}
	}
}

/* Wake charging task on chipset events */
static int pmu_chipset_events(void)
{
	task_wake(TASK_ID_PMU_TPS65090_CHARGER);
	return 0;
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pmu_chipset_events, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pmu_chipset_events, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pmu_chipset_events, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pmu_chipset_events, HOOK_PRIO_DEFAULT);
