/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board_adc.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "driver/ina2xx.h"
#include "gpio/gpio_int.h"
#include "gpio.h"
#include "power_monitor.h"
#include "power.h"
#include "system.h"
#include "timer.h"
#include "task.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define INA236_SHUNT_RESISTOR	1 /* 1 mohm */
#define INA236_ADC_RANGE	2500 /* 2500 nV */
#define INA236_ALERT_LIMIT(x) ((x * INA236_SHUNT_RESISTOR) * 1000 / INA236_ADC_RANGE)

#define INA236_MONITOR_12V_CURRENT 30000 /* 30 A */
#define INA236_MONITOR_5V_CURRENT  2400  /* 2.4 A */

/**
 * We measured the timing between EC turns on the ps_on and recevies the pok_l signal.
 * The average timing is 400 ~ 420 ms.
 */
#define WAIT_PSU_STABLE	500

static bool power_monitor_5vsb_has_alert;

static void power_monitor_enable_interrupt(int idx)
{
	if (idx == INA236_IDX_PSU_12V)
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_power_monitor_0_interrput));
	else if (idx == INA236_IDX_PSU_5V)
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_power_monitor_1_interrput));
	else
		CPRINTS("Unsupport INA236 address index");
}

static void power_monitor_disable_interrupt(int id)
{
	if (id == INA236_IDX_PSU_12V)
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_power_monitor_0_interrput));
	else if (id == INA236_IDX_PSU_5V)
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_power_monitor_1_interrput));
	else
		CPRINTS("Unsupport INA236 address index");
}

void power_monitor_set_5vsb_alert(bool enabled)
{
	power_monitor_5vsb_has_alert = enabled;
}

bool power_monitor_get_5vsb_alert(void)
{
	return power_monitor_5vsb_has_alert;
}

static int power_monitor_update_configuration(int id, uint16_t flags)
{
	uint16_t data;
	int rv;

	data = ina2xx_read(id, INA2XX_REG_CONFIG);

	if (data == 0x0bad) {
		return EC_ERROR_UNKNOWN;
	}

	data |= flags;

	rv = ina2xx_write(INA236_IDX_PSU_12V, INA2XX_REG_CONFIG, data);

	return rv;
}

static void power_monitor_update_ina236(int idx)
{
	int rv, current;

	if (idx == INA236_IDX_PSU_12V)
		current = INA236_MONITOR_12V_CURRENT;
	else if (idx == INA236_IDX_PSU_5V)
		current = INA236_MONITOR_5V_CURRENT;
	else {
		CPRINTS("%s gets invalid index: %d", __func__, idx);
		return;
	}

	rv = power_monitor_update_configuration(idx, INA2XX_CONFIG_AVG_4);

	if (rv != EC_SUCCESS)
		CPRINTS("INA236 index %d write config fail", idx);

	rv = ina2xx_set_alert(idx, INA236_ALERT_LIMIT(current));

	if (rv != EC_SUCCESS)
		CPRINTS("INA236 index %d write alert fail", idx);

	rv = ina2xx_set_mask(idx, INA2XX_MASK_EN_SOL);

	if (rv != EC_SUCCESS)
		CPRINTS("INA236 index %d mask fail", idx);
}

void power_monitor_init(void)
{
	int index;
	int ina236_max_idx = (board_get_version() >= BOARD_VERSION_8) ? 2 : 1;

	for (index = 0; index < ina236_max_idx; index++)
		power_monitor_update_ina236(index);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, power_monitor_init, HOOK_PRIO_DEFAULT);

static void power_monitor_suspend(void)
{
	enum power_state ps = power_get_state();
	int has_alert = !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_5valw_alert_ec_l));


	if (ps == POWER_S0S0ix) {
		/* Set the alert status before enabling the interrupt */
		power_monitor_set_5vsb_alert(has_alert);
		power_monitor_enable_interrupt(INA236_IDX_PSU_5V);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, power_monitor_suspend, HOOK_PRIO_DEFAULT);

static void power_monitor_resume(void)
{
	enum power_state ps = power_get_state();

	if (ps == POWER_S0ixS0) {
		/* Clear the alert status before disabling the interrupt */
		power_monitor_set_5vsb_alert(false);
		power_monitor_disable_interrupt(INA236_IDX_PSU_5V);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, power_monitor_resume, HOOK_PRIO_DEFAULT);

/* Disable the interrupt before powering off the power monitor */
static void power_monitor_shutdown(void)
{
	int index;
	int ina236_max_idx = (board_get_version() >= BOARD_VERSION_8) ? 2 : 1;

	for (index = 0; index < ina236_max_idx; index++)
		power_monitor_disable_interrupt(index);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, power_monitor_shutdown, HOOK_PRIO_DEFAULT);

static void power_monitor_release_index_0(void)
{
	int rv;

	rv = ina2xx_read(INA236_IDX_PSU_12V, INA2XX_REG_MASK);

	if (rv == 0x0bad)
		CPRINTS("ina236 read mask fail");

}
DECLARE_DEFERRED(power_monitor_release_index_0);

/* We don't need to release the alert */
__maybe_unused static void power_monitor_release_index_1(void)
{
	int rv;

	rv = ina2xx_read(INA236_IDX_PSU_5V, INA2XX_REG_MASK);

	if (rv == 0x0bad)
		CPRINTS("ina236 read mask fail");

}
DECLARE_DEFERRED(power_monitor_release_index_1);

static void power_monitor_check_status(void)
{
	bool has_alert = power_monitor_get_5vsb_alert();
	bool power_is_5vsb = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_en_s0ix));

	if ((has_alert && power_is_5vsb) || (!has_alert && !power_is_5vsb))
		task_wake(TASK_ID_CHIPSET);
}
DECLARE_DEFERRED(power_monitor_check_status);

void power_monitor_interrupt_idx_0(enum gpio_signal signal)
{
	hook_call_deferred(&power_monitor_release_index_0_data, 6 * MSEC);
}

void power_monitor_interrupt_idx_1(enum gpio_signal signal)
{
	bool has_alert = !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_5valw_alert_ec_l));
	bool psu_has_enabled = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_pok_l));

	/* EC needs to turn on the psu power as soon as possible. */
	if (has_alert && !psu_has_enabled)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ps_on), 1);

	/**
	 * If the power monitor asserts the alert pin to notice there is
	 * an OCP occurs. Set the alert flag and wake up the chipset task
	 * to turn on the PSU power.
	 */
	power_monitor_set_5vsb_alert(has_alert);
	task_wake(TASK_ID_CHIPSET);

	/**
	 * Call the deferred hook to re-check the status, if the alert pin asserts and
	 * then releases before chipset task idle, EC will not process the enter/exit flow.
	 */
	hook_call_deferred(&power_monitor_check_status_data, WAIT_PSU_STABLE * MSEC);

}
