/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI TPS65090 PMU charging task.
 */

#include "board.h"
#include "chipset.h"
#include "console.h"
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


/* Time delay in usec for idle, charging and discharging.
 * Defined in battery charging flow.
 */
#define T1_USEC 5000000
#define T2_USEC 10000000
#define T3_USEC 10000000

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

static int wait_t1_idle(void)
{
	usleep(T1_USEC);
	return ST_IDLE;
}

static int wait_t2_charging(void)
{
	usleep(T2_USEC);
	return ST_CHARGING;
}

static int wait_t3_discharging(void)
{
	usleep(T3_USEC);
	return ST_DISCHARGING;
}

/*
 * Turn off the host application processor
 */
static int system_off(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		CPUTS("[pmu] turn system off\n");
		chipset_exit_hard_off();

		/* TODO(rongchang): After have impl in chipset_exit_hard_off(),
		 * remove these gpio hack
		 */
		gpio_set_level(GPIO_EN_PP3300, 0);
		gpio_set_level(GPIO_EN_PP1350, 0);
		gpio_set_level(GPIO_PMIC_PWRON_L, 1);
		gpio_set_level(GPIO_EN_PP5000, 0);
	}

	return wait_t1_idle();
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

static int calc_next_state(int state)
{
	int batt_temp, alarm, capacity, charge;

	switch (state) {
	case ST_IDLE:

		/* Check AC and chiset state */
		if (!pmu_get_ac()) {
			if (chipset_in_state(CHIPSET_STATE_ON))
				return ST_DISCHARGING;

			/* Enable charging and wait ac on */
			enable_charging(1);
			return wait_t1_idle();
		}

		/* Enable charging when battery doesn't respond */
		if (battery_temperature(&batt_temp)) {
			enable_charging(1);
			wait_t1_idle();
			return ST_PRE_CHARGING;
		}

		/* Turn off charger when battery temperature is out
		 * of the start charging range.
		 */
		if (!battery_start_charging_range(batt_temp)) {
			enable_charging(0);
			return wait_t1_idle();
		}

		/* Turn off charger on battery charging alarm */
		if (battery_status(&alarm) || (alarm & ALARM_CHARGING)) {
			if (!(alarm & ALARM_TERMINATE_CHARGE))
				CPRINTF("[pmu] idle %016b\n", alarm);
			enable_charging(0);
			return wait_t1_idle();
		}

		/* Start charging only when battery charge lower than 100% */
		if (!battery_state_of_charge(&charge) && charge < 100) {
			enable_charging(1);
			wait_t1_idle();
			return ST_CHARGING;
		}

		return wait_t1_idle();

	case ST_PRE_CHARGING:
		if (!pmu_get_ac())
			return wait_t1_idle();

		/* If the battery goes online after enable the charger,
		 * go into charging state.
		 */
		if (battery_temperature(&batt_temp) == EC_SUCCESS)
			return ST_CHARGING;

		wait_t1_idle();
		return ST_PRE_CHARGING;

	case ST_CHARGING:
		/* Go back to idle state when AC is unplugged */
		if (!pmu_get_ac())
			break;

		/*
		 * Disable charging on battery access error, or charging
		 * temperature out of range.
		 */
		if (battery_temperature(&batt_temp)) {
			CPUTS("[pmu] charging: unable to get battery "
			      "temperature\n");
			enable_charging(0);
			break;
		} else if (!battery_charging_range(batt_temp)) {
			CPRINTF("[pmu] charging: temperature out of range "
				"%dC\n",
				battery_temperature_celsius(batt_temp));
			enable_charging(0);
			break;
		}

		/*
		 * Disable charging on battery alarm events or access error:
		 *   - over temperature
		 *   - over current
		 */
		if (battery_status(&alarm) || (alarm & ALARM_CHARGING)) {
			CPUTS("[pmu] charging: battery alarm\n");
			enable_charging(0);
			break;
		}

		/*
		 * Disable charging on charger alarm events:
		 *   - charger over current
		 *   - charger over temperature
		 */
		if (pmu_is_charger_alarm()) {
			CPUTS("[pmu] charging: charger alarm\n");
			enable_charging(0);
			break;
		}

		return wait_t2_charging();

	case ST_DISCHARGING:
		/* Go back to idle state when AC is plugged */
		if (pmu_get_ac())
			return wait_t1_idle();

		/* Check battery discharging temperature range */
		if (battery_temperature(&batt_temp) == 0) {
			if (!battery_discharging_range(batt_temp)) {
				CPRINTF("[pmu] discharging: temperature out of"
					"range %dC\n",
					battery_temperature_celsius(batt_temp));
				enable_charging(0);
				return system_off();
			}
		}
		/* Check discharging alarm */
		if (!battery_status(&alarm) && (alarm & ALARM_DISCHARGING)) {
			CPRINTF("[pmu] discharging: battery alarm %016b\n",
					alarm);
			enable_charging(0);
			return system_off();
		}
		/* Check remaining charge % */
		if (battery_state_of_charge(&capacity) == 0 && capacity < 10)
			return notify_battery_low();

		return wait_t3_discharging();
	}

	return wait_t1_idle();
}

void pmu_charger_task(void)
{
	int state = ST_IDLE;
	int next_state;

	while (1) {
		pmu_clear_irq();

		next_state = calc_next_state(state);
		if (next_state != state) {
			CPRINTF("[batt] state %s -> %s\n",
				state_list[state],
				state_list[next_state]);
			state = next_state;
		}

		/* TODO(sjg@chromium.org): root cause crosbug.com/p/11285 */
		task_wait_event(5000 * 1000);
	}
}
