/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "board_host_command.h"
#include "board_function.h"
#include "chipset.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "diagnostics.h"
#include "ec_commands.h"
#include "extpower.h"
#include "flash_storage.h"
#include "gpio/gpio_int.h"
#include "gpio.h"
#include "hooks.h"
#include "power_button.h"
#include "system.h"
#include "temp_sensor.h"
#include "util.h"
#include "zephyr_console_shim.h"

/* counter for chassis open while ec no power, only rtc power */
static uint8_t chassis_vtr_open_count;
/* counter for chassis open while ec has power */
static uint8_t chassis_open_count;
/* counter for chassis press while ec has power, clear when enter S0 */
static uint8_t chassis_press_counter;
/* make sure only trigger once */
static uint8_t chassis_once_flag;
static uint64_t chassis_open_hibernate_time;
static uint8_t init = 1;

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_GPIO, format, ##args)

static void check_chassis_open(void);
DECLARE_DEFERRED(check_chassis_open);

static void chassis_init(void)
{
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_chassis_open));

	init = 1;
	check_chassis_open();
	init = 0;
}
DECLARE_HOOK(HOOK_INIT, chassis_init, HOOK_PRIO_DEFAULT + 1);

static void chassis_open_hibernate(void)
{
	uint64_t now;
	int chassis_status = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l));

	/* We don't need to hibernate EC when extpower is present or chassis is closed */
	if (extpower_is_present() || chassis_status || !chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return;

	/* EC does not update the chassis open hibernate timer, ignore it */
	if (!chassis_open_hibernate_time)
		return;

	now = get_time().val;
	CPRINTS("chassis_open_hibernate_time:%lld, now:%lld", chassis_open_hibernate_time, now);
	if (now > chassis_open_hibernate_time) {
		CPRINTS("Chassis open hibernate");
		system_hibernate(0, 0);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, chassis_open_hibernate, HOOK_PRIO_DEFAULT);
DECLARE_DEFERRED(chassis_open_hibernate);

static void check_chassis_open(void)
{
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l)) == 0) {
		CPRINTS("Chassis was opened");
		/* Record the chassis was open status in bbram */
		if (!chassis_once_flag)
			system_set_bbram(SYSTEM_BBRAM_IDX_CHASSIS_WAS_OPEN, 1);

		chassis_once_flag = 1;

		if (init) {
			system_get_bbram(SYSTEM_BBRAM_IDX_CHASSIS_VTR_OPEN,
				&chassis_vtr_open_count);
			if (chassis_vtr_open_count < 0xFF)
				chassis_vtr_open_count++;
			system_set_bbram(SYSTEM_BBRAM_IDX_CHASSIS_VTR_OPEN, chassis_vtr_open_count);
		} else {
			system_get_bbram(SYSTEM_BBRAM_IDX_CHASSIS_TOTAL,
				&chassis_open_count);
			if (chassis_open_count < 0xFF)
				chassis_open_count++;
			system_set_bbram(SYSTEM_BBRAM_IDX_CHASSIS_TOTAL, chassis_open_count);
		}

		/* Counter for chassis pin */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			if (chassis_press_counter < 0xFF)
				chassis_press_counter++;
	} else if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l)) == 1
			&& chassis_once_flag) {

		CPRINTS("Chassis was closed");
		chassis_once_flag = 0;
	}

	hook_call_deferred(&chassis_open_hibernate_data, 0);
}

__override enum critical_shutdown
board_system_is_idle(uint64_t last_shutdown_time, uint64_t *target,
		     uint64_t now)
{
	/* update the chassis open target time = 30s - 28s*/
	chassis_open_hibernate_time = *target - 28000000;

	/* After setting the chassis open hibernate timer, delay 2.5s to check the chassis status */
	hook_call_deferred(&chassis_open_hibernate_data, 2500 * MSEC);

	if (now < *target)
		return CRITICAL_SHUTDOWN_IGNORE;

	CPRINTS("SDC Safe");
	return CRITICAL_SHUTDOWN_HIBERNATE;
}

__overridable void project_chassis_function(enum gpio_signal signal)
{
}

void chassis_interrupt_handler(enum gpio_signal signal)
{
	project_chassis_function(signal);
	hook_call_deferred(&check_chassis_open_data, 50 * MSEC);
}

static int chassis_cmd_clear(int type)
{
	int press;

	if (type) {
		/* clear when host cmd send magic value */
		chassis_vtr_open_count = 0;
		chassis_open_count = 0;
	} else {
		/* clear when bios get, bios will get this data while post */
		press = chassis_press_counter;
		chassis_press_counter = 0;
		return press;
	}
	return -1;
}

/* Host command */
static enum ec_status host_chassis_intrusion_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_chassis_intrusion_control *p = args->params;
	struct ec_response_chassis_intrusion_control *r = args->response;

	if (p->clear_magic == EC_PARAM_CHASSIS_INTRUSION_MAGIC) {
		chassis_cmd_clear(1);
		system_set_bbram(SYSTEM_BBRAM_IDX_CHASSIS_TOTAL, 0);
		system_set_bbram(SYSTEM_BBRAM_IDX_CHASSIS_VTR_OPEN, 0);
		system_set_bbram(SYSTEM_BBRAM_IDX_CHASSIS_MAGIC, EC_PARAM_CHASSIS_BBRAM_MAGIC);
		return EC_SUCCESS;
	}

	if (p->clear_chassis_status) {
		system_set_bbram(SYSTEM_BBRAM_IDX_CHASSIS_WAS_OPEN, 0);
		return EC_SUCCESS;
	}

	system_get_bbram(SYSTEM_BBRAM_IDX_CHASSIS_WAS_OPEN, &r->chassis_ever_opened);
	system_get_bbram(SYSTEM_BBRAM_IDX_CHASSIS_MAGIC, &r->coin_batt_ever_remove);
	system_get_bbram(SYSTEM_BBRAM_IDX_CHASSIS_TOTAL, &r->total_open_count);
	system_get_bbram(SYSTEM_BBRAM_IDX_CHASSIS_VTR_OPEN, &r->vtr_open_count);

	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHASSIS_INTRUSION, host_chassis_intrusion_control,
			EC_VER_MASK(0));

static enum ec_status chassis_open_check(struct host_cmd_handler_args *args)
{
	struct ec_response_chassis_open_check *r = args->response;
	int status = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l));

	r->status = !status & 0x01;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;

}
DECLARE_HOST_COMMAND(EC_CMD_CHASSIS_OPEN_CHECK, chassis_open_check, EC_VER_MASK(0));

static enum ec_status chassis_counter(struct host_cmd_handler_args *args)
{
	struct ec_response_chassis_counter *r = args->response;

	r->press_counter = chassis_cmd_clear(0);
	CPRINTS("Read chassis counter: %d", r->press_counter);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHASSIS_COUNTER, chassis_counter, EC_VER_MASK(0));
