/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "board_host_command.h"
#include "chipset.h"
#include "console.h"
#include "cpu_power.h"
#include "customized_shared_memory.h"
#include "cypress_pd_common.h"
#include "ec_commands.h"
#include "factory.h"
#include "fan.h"
#include "flash_storage.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "lpc.h"
#include "power_sequence.h"
#include "system.h"
#include "util.h"
#include "zephyr_console_shim.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ##args)

static void sci_enable(void);
DECLARE_DEFERRED(sci_enable);
static void sci_enable(void)
{
	if (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) & BIT(0)) {
		/* when host set EC driver ready flag, EC need to enable SCI */
		lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, SCI_HOST_EVENT_MASK);
	} else
		hook_call_deferred(&sci_enable_data, 250 * MSEC);
}

static void sci_disable(void)
{
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, sci_disable, HOOK_PRIO_DEFAULT);

static enum ec_status flash_notified(struct host_cmd_handler_args *args)
{

	const struct ec_params_flash_notified *p = args->params;

	switch (p->flags & 0x03) {
	case FLASH_FIRMWARE_START:
		CPRINTS("Start flashing firmware, flags:0x%02x", p->flags);
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_open));

		if ((p->flags & FLASH_FLAG_PD) == FLASH_FLAG_PD) {
			gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip0_interrupt));
			gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip1_interrupt));
			set_pd_fw_update(true);
		}
	case FLASH_ACCESS_SPI:
		break;

	case FLASH_FIRMWARE_DONE:
		CPRINTS("Flash done, flags:0x%02x", p->flags);
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_powerbtn));
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip0_interrupt));
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip1_interrupt));
		gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_open));

		set_pd_fw_update(false);
		/* resetup PD controllers */
		if ((p->flags & FLASH_FLAG_PD) == FLASH_FLAG_PD)
			cypd_reinitialize();

	case FLASH_ACCESS_SPI_DONE:
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FLASH_NOTIFIED, flash_notified,
			EC_VER_MASK(0));

static enum ec_status factory_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_factory_notified *p = args->params;
	int enable = 1;


	if (p->flags)
		factory_setting(enable);
	else
		factory_setting(!enable);

	if (p->flags == RESET_FOR_SHIP) {
		/* clear bbram for shipping */
		system_set_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, 0);
		flash_storage_load_defaults();
		flash_storage_commit();
	}

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FACTORY_MODE, factory_mode,
			EC_VER_MASK(0));

static enum ec_status hc_pwm_get_fan_actual_rpm(struct host_cmd_handler_args *args)
{
	const struct ec_params_ec_pwm_get_actual_fan_rpm *p = args->params;
	struct ec_response_pwm_get_actual_fan_rpm *r = args->response;

	if (FAN_CH_COUNT == 0 || p->index >= FAN_CH_COUNT)
		return EC_ERROR_INVAL;

	r->rpm = fan_get_rpm_actual(FAN_CH(p->index));
	args->response_size = sizeof(*r);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PWM_GET_FAN_ACTUAL_RPM,
		     hc_pwm_get_fan_actual_rpm,
		     EC_VER_MASK(0));

static enum ec_status enter_non_acpi_mode(struct host_cmd_handler_args *args)
{
	/**
	 * TODO:
	 * When system boot into OS, host will call this command to verify,
	 * It means system should in S0 state, and we need to set the resume
	 * S0ix flag to avoid the wrong state when unknown reason warm boot.
	 */
	if (chipset_in_state(CHIPSET_STATE_STANDBY))
		*host_get_memmap(EC_CUSTOMIZED_MEMMAP_POWER_STATE) |= EC_PS_RESUME_S0ix;

	/**
	 * When system reboot and go into setup menu, we need to set the power_s5_up flag
	 * to wait SLP_S5 and SLP_S3 signal to boot into OS.
	 */
	power_s5_up_control(1);

	/**
	 * Even though the protocol returns EC_SUCCESS,
	 * the system still does not update the power limit.
	 * So move the update process at here.
	 */
	update_soc_power_limit(true, false);

	power_state_clear(EC_PS_ENTER_S4 | EC_PS_RESUME_S4 |
		EC_PS_ENTER_S5 | EC_PS_RESUME_S5);

	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) &= ~BIT(0);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_NON_ACPI_NOTIFY, enter_non_acpi_mode, EC_VER_MASK(0));

static enum ec_status enter_acpi_mode(struct host_cmd_handler_args *args)
{
	hook_call_deferred(&sci_enable_data, 250 * MSEC);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_ACPI_NOTIFY, enter_acpi_mode, EC_VER_MASK(0));

static enum ec_status read_pd_versoin(struct host_cmd_handler_args *args)
{
	struct ec_response_read_pd_version *r = args->response;

	memcpy(r->pd0_version, get_pd_version(0), sizeof(r->pd0_version));
	memcpy(r->pd1_version, get_pd_version(1), sizeof(r->pd1_version));

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_READ_PD_VERSION, read_pd_versoin, EC_VER_MASK(0));

static enum ec_status  host_command_get_simple_version(struct host_cmd_handler_args *args)
{
	struct ec_response_get_custom_version *r = args->response;
	char temp_version[32];
	int idx;

	strzcpy(temp_version, system_get_version(EC_IMAGE_RO),
		sizeof(temp_version));

	for (idx = 0; idx < sizeof(r->simple_version); idx++) {
		r->simple_version[idx] = temp_version[idx + 18];
	}

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_SIMPLE_VERSION, host_command_get_simple_version, EC_VER_MASK(0));

/*******************************************************************************/
/*                       EC console command for Project                        */
/*******************************************************************************/
static int cmd_bbram(int argc, const char **argv)
{
	uint8_t bbram;
	uint8_t ram_addr;
	char *e;

	if (argc > 1) {
		ram_addr = strtoi(argv[1], &e, 0);
		system_get_bbram(ram_addr, &bbram);
		CPRINTF("BBram%d: %d", ram_addr, bbram);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bbram, cmd_bbram,
			"[bbram address]",
			"get bbram data with hibdata_index");
