/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "board_host_command.h"
#include "board_function.h"
#include "chipset.h"
#include "console.h"
#include "common_cpu_power.h"
#include "customized_shared_memory.h"
#include "cypress_pd_common.h"
#include "diagnostics.h"
#include "ec_commands.h"
#include "factory.h"
#include "fan.h"
#include "flash_storage.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "keyboard_8042_sharedlib.h"
#include "keyboard_8042.h"
#include "led.h"
#include "lpc.h"
#include "power_sequence.h"
#include "system.h"
#include "uefi_app_mode.h"
#include "util.h"
#include "zephyr_console_shim.h"

#ifdef CONFIG_BOARD_LOTUS
#include "gpu.h"
#include "input_module.h"
#endif

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_HOSTCMD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ##args)

static void sci_enable(void);
DECLARE_DEFERRED(sci_enable);

#ifdef CONFIG_BOARD_LOTUS
static void gpu_typec_detect(void)
{
	set_host_dp_ready(1);
}
DECLARE_DEFERRED(gpu_typec_detect);
#endif

static void sci_enable(void)
{
	if (*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) & ACPI_DRIVER_READY) {
		/* when host set EC driver ready flag, EC need to enable SCI */
		lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, SCI_HOST_EVENT_MASK);
		bios_function_detect();
#ifdef CONFIG_BOARD_LOTUS
		/* hook_call_deferred(&gpu_typec_detect_data, 500 * MSEC); */
		gpu_typec_detect();
#endif
	} else
		hook_call_deferred(&sci_enable_data, 250 * MSEC);
}

static void sci_disable(void)
{
	lpc_set_host_event_mask(LPC_HOST_EVENT_SCI, 0);
#ifdef CONFIG_BOARD_LOTUS
	set_host_dp_ready(0);
#endif
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

	clear_power_flags();

	*host_get_memmap(EC_CUSTOMIZED_MEMMAP_SYSTEM_FLAGS) &= ~ACPI_DRIVER_READY;
	*host_get_memmap(EC_MEMMAP_POWER_SLIDE) = 0x0;
	*host_get_memmap(EC_MEMMAP_STT_TABLE_NUMBER) = 0x0;

	update_apu_ready(1);

	/**
	 * Even though the protocol returns EC_SUCCESS,
	 * the system still does not update the power limit.
	 * So move the update process at here.
	 */
	update_soc_power_limit(true, false);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_NON_ACPI_NOTIFY, enter_non_acpi_mode, EC_VER_MASK(0));

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

static enum ec_status cmd_diagnosis(struct host_cmd_handler_args *args)
{

	const struct ec_params_diagnosis *p = args->params;

	set_bios_diagnostic(p->diagnosis_code);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_DIAGNOSIS, cmd_diagnosis,
			EC_VER_MASK(0));

static enum ec_status cmd_get_hw_diag(struct host_cmd_handler_args *args)
{
	struct ec_response_get_hw_diag *r = args->response;

	r->hw_diagnostics = get_hw_diagnostic();
	r->bios_complete = is_bios_complete();
	r->device_complete = is_device_complete();

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_HW_DIAG, cmd_get_hw_diag,
			EC_VER_MASK(0));

#ifdef CONFIG_PLATFORM_EC_KEYBOARD
static enum ec_status update_keyboard_matrix(struct host_cmd_handler_args *args)
{
	const struct ec_params_update_keyboard_matrix *p = args->params;
	struct ec_params_update_keyboard_matrix *r = args->response;

	int i;

	if (p->num_items > 32) {
		return EC_ERROR_INVAL;
	}
	if (p->write) {
		for (i = 0; i < p->num_items; i++) {
			set_scancode_set2(p->scan_update[i].row, p->scan_update[i].col
					, p->scan_update[i].scanset);
		}
	}
	r->num_items = p->num_items;
	for (i = 0; i < p->num_items; i++) {
		r->scan_update[i].row = p->scan_update[i].row;
		r->scan_update[i].col = p->scan_update[i].col;
		r->scan_update[i].scanset = get_scancode_set2(p->scan_update[i].row
			, p->scan_update[i].col);
	}
	args->response_size = sizeof(struct ec_params_update_keyboard_matrix);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_UPDATE_KEYBOARD_MATRIX, update_keyboard_matrix, EC_VER_MASK(0));
#endif

static enum ec_status fp_led_level_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_led_control *p = args->params;
	struct ec_response_fp_led_level *r = args->response;
	uint8_t led_level = FP_LED_HIGH;

	if (p->get_led_level) {
		system_get_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, &r->level);
		args->response_size = sizeof(*r);
		return EC_RES_SUCCESS;
	}

	switch (p->set_led_level) {
	case FP_LED_BRIGHTNESS_HIGH:
		led_level = FP_LED_HIGH;
		break;
	case FP_LED_BRIGHTNESS_MEDIUM:
		led_level = FP_LED_MEDIUM;
		break;
	case FP_LED_BRIGHTNESS_LOW:
		led_level = FP_LED_LOW;
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}

	system_set_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, led_level);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_LED_LEVEL_CONTROL, fp_led_level_control, EC_VER_MASK(0));

static enum ec_status chassis_open_check(struct host_cmd_handler_args *args)
{
	struct ec_response_chassis_open_check *r = args->response;
	int status = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l));

	r->status = !status & 0x01;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;

}
DECLARE_HOST_COMMAND(EC_CMD_CHASSIS_OPEN_CHECK, chassis_open_check, EC_VER_MASK(0));

static enum ec_status enter_acpi_mode(struct host_cmd_handler_args *args)
{
	hook_call_deferred(&sci_enable_data, 250 * MSEC);

#ifdef CONFIG_BOARD_LOTUS
	/* Need to cleanly enumerate keyboard in OS, otherwise NKRO won't work without hotplug */
	/* So we reset the input modules right on entry to OS */
	input_modules_reset();
#endif

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

static enum ec_status standalone_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_standalone_mode *p = args->params;

	set_standalone_mode((int)p->enable);
	return EC_RES_SUCCESS;

}
DECLARE_HOST_COMMAND(EC_CMD_STANDALONE_MODE, standalone_mode, EC_VER_MASK(0));

static enum ec_status chassis_counter(struct host_cmd_handler_args *args)
{
	struct ec_response_chassis_counter *r = args->response;

	r->press_counter = chassis_cmd_clear(0);
	CPRINTS("Read chassis counter: %d", r->press_counter);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHASSIS_COUNTER, chassis_counter, EC_VER_MASK(0));

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

static enum ec_status get_active_charge_pd_chip(struct host_cmd_handler_args *args)
{
	struct ec_response_get_active_charge_pd_chip *r = args->response;

	r->pd_chip = active_charge_pd_chip();
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_ACTIVE_CHARGE_PD_CHIP, get_active_charge_pd_chip, EC_VER_MASK(0));

#ifdef CONFIG_BOARD_LOTUS
static enum ec_status host_command_uefi_app_mode(struct host_cmd_handler_args *args)
{
	const struct ec_params_uefi_app_mode *p = args->params;
	int enable = 1;

	if (p->flags)
		uefi_app_mode_setting(enable);
	else
		uefi_app_mode_setting(!enable);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_UEFI_APP_MODE, host_command_uefi_app_mode, EC_VER_MASK(0));

static enum ec_status host_command_uefi_app_btn_status(struct host_cmd_handler_args *args)
{
	struct ec_response_uefi_app_btn_status *r = args->response;

	r->status = uefi_app_btn_status();

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_UEFI_APP_BTN_STATUS, host_command_uefi_app_btn_status, EC_VER_MASK(0));

static enum ec_status hc_fingerprint_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_fingerprint_control *p = args->params;
	int enable = 1;

	if (p->enable)
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fp_en), enable);
	else
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_fp_en), !enable);

	return EC_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_CONTROL, hc_fingerprint_control, EC_VER_MASK(0));
#endif /* CONFIG_BOARD_LOTUS */

static enum ec_status privacy_switches_check(struct host_cmd_handler_args *args)
{
	struct ec_response_privacy_switches_check *r = args->response;

	/*
	 * Camera is low when off, microphone is high when off
	 * Return 0 when off/close and 1 when high/open
	 */
	r->microphone = !gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_mic_sw));
	r->camera = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_cam_sw));

	CPRINTS("Microphone switch open: %d", r->microphone);
	CPRINTS("Camera switch open: %d", r->camera);

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;

}
DECLARE_HOST_COMMAND(EC_CMD_PRIVACY_SWITCHES_CHECK_MODE, privacy_switches_check, EC_VER_MASK(0));

#ifdef CONFIG_CHIPSET_INTEL
static enum ec_status disable_ps2_mouse_emulation(struct host_cmd_handler_args *args)
{
	const struct ec_params_ps2_emulation_control *p = args->params;

	/**
	 * TODO: ps2 mouse emulation
	 * set_ps2_mouse_emulation(p->disable);
	 */
	CPRINTS("TODO: ps2 mouse emulation %d", p->disable);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_DISABLE_PS2_EMULATION, disable_ps2_mouse_emulation, EC_VER_MASK(0));

#ifdef PD_CHIP_CCG6
static enum ec_status bb_retimer_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_bb_retimer_control_mode *p = args->params;
	struct ec_response_bb_retimer_control_mode *r = args->response;

	CPRINTS("TODO: bb-retimer interface :%d", p->controller);
	r->status = 0;
	args->response_size = sizeof(*r);

	/**
	 * TODO: wait CCG6 interface
	 * Note: retimer control register is multi declared in CCG8 and CCG6
	 */

	/*switch (p->modes) {
	case BB_ENTRY_FW_UPDATE_MODE:
		entry_tbt_mode(p->controller);
		break;
	case BB_EXIT_FW_UPDATE_MODE:
		exit_tbt_mode(p->controller);
		break;
	case BB_ENABLE_COMPLIANCE_MODE:
		enable_compliance_mode(p->controller);
		break;
	case BB_DISABLE_COMPLIANCE_MODE:
		disable_compliance_mode(p->controller);
		break;
	case BB_CHECK_STATUS:
		r->status = check_tbt_mode(p->controller);
		args->response_size = sizeof(*r);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}*/

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BB_RETIMER_CONTROL, bb_retimer_control, EC_VER_MASK(0));
#endif /* PD_CHIP_CCG6 */
#endif /* CONFIG_CHIPSET_INTEL */
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

static int cmd_memmap(int argc, const char **argv)
{
	int data;
	int offset;
	char *e;

	if (argc > 3) {
		offset = strtoi(argv[2], &e, 0);
		data = strtoi(argv[3], &e, 0);
		*host_get_memmap(offset) = data;
		CPRINTS("EC_MEMMAP offset:%d, set data:%d", offset, data);
	} else {
		offset = strtoi(argv[2], &e, 0);
		data = *host_get_memmap(offset);
		CPRINTS("EC_MEMMAP offset:%d, get data:%d", offset, data);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(memmap, cmd_memmap,
			"[memmap get/set offset [value]]",
			"get/set memmap data");
