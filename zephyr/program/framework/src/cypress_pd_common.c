/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"

#include "battery.h"
#include "board_function.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "common_cpu_power.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "flash_storage.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "power.h"
#include "power_button.h"
#include "raa489300.h"
#include "task.h"
#include "ucsi.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_tc_sm.h"
#include "usb_emsg.h"
#include "util.h"
#include "throttle_ap.h"
#include "zephyr_console_shim.h"

#ifdef CONFIG_PLATFORM_EC_FRAMEWORK_LAPTOP_16
#include "gpu.h"
#include "cpu_power.h"
#endif

#ifndef CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC
#include "diagnostics_laptop.h"
#endif

#include <zephyr/sys_clock.h>

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

struct alert_msg_t alert_rx[CONFIG_USB_PD_PORT_MAX_COUNT];
struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

static int prev_charge_port = -1;
static bool verbose_msg_logging;
static bool firmware_update;
static bool alert_press;
static int pre_safety_level = TYPEC_SAFETY_LEVEL_0;

/**
 * Delay 500 ms to start updating the battery information
 */
#define READY_BATTERY_UPDATE 500

/*****************************************************************************/
/* Internal functions */

static void cypd_pdo_reset_deferred(void);
static void cypd_set_prepare_pdo(int controller, int port);

static bool cypd_controller_is_error(int controller)
{
	if (pd_chip_config[controller].state == CCG_STATE_ERROR)
		return true;

	return false;
}

/**
 * If the PD chip is in the bootloader mode, EC shouldn't send the cmd
 * to PD chip.
 */
static bool cypd_controller_in_bootloader(int controller)
{
	if (pd_chip_config[controller].state == CCG_STATE_BOOTLOADER)
		return true;

	return false;
}

/**
 * The EC should not send unsupported commands while the PD is in bootloader mode.
 */
static bool cypd_allow_reg_in_bootloader(int reg)
{
	int bootloader_reg[4] = {
		CCG_DEVICE_MODE, CCG_BOOT_MODE_REASON, CCG_RESPONSE_REG, CCG_INTR_REG
	};

	for (int idx = 0; idx < ARRAY_SIZE(bootloader_reg); idx++) {
		if (bootloader_reg[idx] == reg)
			return true;
	}

	return false;
}

static bool cypd_controllers_are_ready(void)
{
	for (int controller = 0; controller < PD_CHIP_COUNT; controller++) {
		if (pd_chip_config[controller].state == CCG_STATE_NO_POWER)
			continue;

		if (pd_chip_config[controller].state != CCG_STATE_READY)
			return false;
	}

	return true;
}

bool cypd_contoller_is_powered(int controller)
{
	if (pd_chip_config[controller].state == CCG_STATE_NO_POWER)
		return false;

	return true;
}

bool ccg8s_init(uint8_t address, uint32_t flags)
{
	/* we assume this is port 5 */
	pd_chip_config[PD_CHIP_GPU].addr_flags = address | I2C_FLAG_ADDR16_LITTLE_ENDIAN;

	/* only update the POWER_ON pd chip state once the pd chip is not ready */
	if (address && pd_chip_config[PD_CHIP_GPU].state != CCG_STATE_READY) {
		cypd_update_chips_state(PD_CHIP_GPU, CCG_STATE_POWER_ON);
		task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_GPU | CCG_EVT_BATT_UPDATE);
	} else if (!address) {
		cypd_update_chips_state(PD_CHIP_GPU, CCG_STATE_NO_POWER);
		task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_GPU | CCG_EVT_BATT_UPDATE);
	}
	return true;
}

void ccg8s_interrupt(enum gpio_signal signal)
{
	if (pd_chip_config[PD_CHIP_GPU].state != CCG_STATE_NO_POWER)
		task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_GPU);
}

int cypd_write_reg_block(int controller, int reg, void *data, int len)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	if (controller >= PD_CHIP_COUNT)
		return EC_ERROR_PARAM1;

	if (cypd_controller_is_error(controller))
		return EC_ERROR_UNKNOWN;

	/* EC shouldn't communicate with PD chip during it is updating */
	if (cypd_fw_update_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	if (cypd_controller_in_bootloader(controller) && !cypd_allow_reg_in_bootloader(reg)) {
		CPRINTS("PD%d is in boot mode, pending write block:0x%04x", controller, reg);
		return EC_ERROR_ACCESS_DENIED;
	}

	rv = i2c_write_offset16_block(i2c_port, addr_flags, reg, data, len);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_write_reg16(int controller, int reg, int data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	if (controller >= PD_CHIP_COUNT)
		return EC_ERROR_PARAM1;

	if (cypd_controller_is_error(controller))
		return EC_ERROR_UNKNOWN;

	/* EC shouldn't communicate with PD chip during it is updating */
	if (cypd_fw_update_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	if (cypd_controller_in_bootloader(controller) && !cypd_allow_reg_in_bootloader(reg)) {
		CPRINTS("PD%d is in boot mode, pending write reg16:0x%04x", controller, reg);
		return EC_ERROR_ACCESS_DENIED;
	}

	rv = i2c_write_offset16(i2c_port, addr_flags, reg, data, 2);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_write_reg8(int controller, int reg, int data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	if (controller >= PD_CHIP_COUNT)
		return EC_ERROR_PARAM1;

	if (cypd_controller_is_error(controller))
		return EC_ERROR_UNKNOWN;

	/* EC shouldn't communicate with PD chip during it is updating */
	if (cypd_fw_update_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	if (cypd_controller_in_bootloader(controller) && !cypd_allow_reg_in_bootloader(reg)) {
		CPRINTS("PD%d is in boot mode, pending write reg8:0x%04x", controller, reg);
		return EC_ERROR_ACCESS_DENIED;
	}

	rv = i2c_write_offset16(i2c_port, addr_flags, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_read_reg_block(int controller, int reg, void *data, int len)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	if (controller >= PD_CHIP_COUNT)
		return EC_ERROR_PARAM1;

	if (cypd_controller_is_error(controller))
		return EC_ERROR_UNKNOWN;

	/* EC shouldn't communicate with PD chip during it is updating */
	if (cypd_fw_update_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	if (cypd_controller_in_bootloader(controller) && !cypd_allow_reg_in_bootloader(reg)) {
		CPRINTS("PD%d is in boot mode, pending read block:0x%04x", controller, reg);
		return EC_ERROR_ACCESS_DENIED;
	}

	rv = i2c_read_offset16_block(i2c_port, addr_flags, reg, data, len);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_read_reg16(int controller, int reg, int *data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	if (controller >= PD_CHIP_COUNT)
		return EC_ERROR_PARAM1;

	if (cypd_controller_is_error(controller))
		return EC_ERROR_UNKNOWN;

	/* EC shouldn't communicate with PD chip during it is updating */
	if (cypd_fw_update_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	if (cypd_controller_in_bootloader(controller) && !cypd_allow_reg_in_bootloader(reg)) {
		CPRINTS("PD%d is in boot mode, pending read reg16:0x%04x", controller, reg);
		return EC_ERROR_ACCESS_DENIED;
	}

	rv = i2c_read_offset16(i2c_port, addr_flags, reg, data, 2);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_read_reg8(int controller, int reg, int *data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	if (controller >= PD_CHIP_COUNT)
		return EC_ERROR_PARAM1;

	if (cypd_controller_is_error(controller))
		return EC_ERROR_UNKNOWN;

	/* EC shouldn't communicate with PD chip during it is updating */
	if (cypd_fw_update_in_progress())
		return EC_ERROR_ACCESS_DENIED;

	if (cypd_controller_in_bootloader(controller) && !cypd_allow_reg_in_bootloader(reg)) {
		CPRINTS("PD%d is in boot mode, pending read reg8:0x%04x", controller, reg);
		return EC_ERROR_ACCESS_DENIED;
	}

	rv = i2c_read_offset16(i2c_port, addr_flags, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_read_respond(int controller, int reg, int *data)
{
	int intr_status;
	int cmd_type;
	int rv = EC_SUCCESS;

	if (reg < 0x1000)
		cmd_type = CCG_DEV_INTR;
	else if (reg < 0x2000)
		cmd_type = CCG_PORT0_INTR;
	else
		cmd_type = CCG_PORT1_INTR;

	if (cypd_get_int(controller, &intr_status))
		return EC_ERROR_INVAL;

	if (intr_status & CCG_DEV_INTR && cmd_type == CCG_DEV_INTR)
		rv = cypd_read_reg16(controller, CCG_RESPONSE_REG, data);
	else if (intr_status & CCG_PORT0_INTR && cmd_type == CCG_PORT0_INTR)
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(0), data);
	else if (intr_status & CCG_PORT1_INTR && cmd_type == CCG_PORT1_INTR)
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(1), data);
	else {
		if (verbose_msg_logging) {
			cypd_read_reg16(controller, CCG_RESPONSE_REG, data);
			CPRINTS("Dev 0x%x", *data);
			cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(0), data);
			CPRINTS("P0 0x%x", *data);
			cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(1), data);
			CPRINTS("P1 0x%x", *data);
		}
		rv = EC_ERROR_INVAL;
	}

	if (rv != EC_SUCCESS)
		CPRINTS("Fail to read the 0x%04x response", reg);

	cypd_clear_int(controller, cmd_type);

	return rv;
}

int cypd_write_reg_with_respond(int controller, int reg, int data, int *respond_code)
{
	const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[controller].gpio);
	int intr_status;
	int rv = EC_ERROR_UNKNOWN;
	int retry_count = 0;

	if (controller < 0 || controller >= PD_CHIP_COUNT)
		return EC_ERROR_INVAL;

	/* Clear the current or pending interrupt before issuing a new command */
	if (gpio_pin_get_dt(intr) == 0) {
		if (cypd_get_int(controller, &intr_status))
			return EC_ERROR_INVAL;
		if (cypd_clear_int(controller, intr_status))
			return EC_ERROR_INVAL;
		crec_usleep(50);
	}

cypd_cmd_retry:
	if (cypd_write_reg8(controller, reg, data)) {
		CPRINTS("CYPD writes cmd:0x%04x fail!", reg);
		return EC_ERROR_INVAL;
	}

	if (cypd_wait_for_ack(controller, 100) != EC_SUCCESS) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}

	if (cypd_read_respond(controller, reg, respond_code))
		return EC_ERROR_INVAL;

	/* Process the respond code */
	switch (*respond_code) {
	case CCG_RESPONSE_PD_COMMAND_FAILED:
		crec_msleep(1);
		if (retry_count++ < 10)
			goto cypd_cmd_retry;
		else
			CPRINTS("CYPD cmd:0x%04x got resp code:0x%04x; Retry 10 times failed.",
				reg, *respond_code);
		break;
	case CCG_RESPONSE_SUCCESS:
		rv = EC_SUCCESS;
		break;
	default:
		CPRINTS("CYPD cmd:0x%04x respond code: 0x%04x", reg, *respond_code);
		break;
	}

	return rv;
}

static int cypd_reset(int controller)
{
	/*
	 * Device Reset: This command is used to request the CCG device to perform a soft reset
	 * and start at the boot-loader stage again
	 * Note: need barrel AC or battery
	 */
	return cypd_write_reg16(controller, CCG_RESET_REG, CCG_RESET_CMD);
}

int cypd_get_int(int controller, int *intreg)
{
	int rv;

	rv = cypd_read_reg8(controller, CCG_INTR_REG, intreg);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, rv=0x%02x", __func__, controller, rv);
	return rv;
}

int cypd_clear_int(int controller, int mask)
{
	int rv;

	rv = cypd_write_reg8(controller, CCG_INTR_REG, mask);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, mask=0x%02x", __func__, controller, mask);
	return rv;
}

int cypd_wait_for_ack(int controller, int timeout_ms)
{
	const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[controller].gpio);
	timestamp_t start = get_time();

	/* wait for interrupt ack to be asserted */
	do {
		if (gpio_pin_get_dt(intr) == 0)
			break;
		crec_usleep(100);
	} while (time_since32(start) < (timeout_ms * MSEC));

	/* make sure response is ok */
	if (gpio_pin_get_dt(intr) != 0) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

void cypd_print_buff(const char *msg, void *buff, int len)
{
	int i;
	uint8_t *data = (uint8_t *)buff;

	CPRINTF("[           %s 0x", msg);
	for (i = len-1; i >= 0; i--) {
		CPRINTF("%02x", data[i]);
	}
	CPRINTF(" ]\n");
}

#ifdef CONFIG_BOARD_LOTUS
static void update_external_cc_mux(int port, int cc)
{
	if (port == 1) {
		switch(cc) {
			case POLARITY_CC1:
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc1), 1);
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc2), 0);
				break;

			case POLARITY_CC2:
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc1), 0);
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc2), 1);
				break;

			default:
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc1), 0);
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_usb3_ec_p2_cc2), 0);
		}
	}
}
#endif

static void pd0_update_state_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0);
}
DECLARE_DEFERRED(pd0_update_state_deferred);

static void pd1_update_state_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_1);

}
DECLARE_DEFERRED(pd1_update_state_deferred);

static void pd_gpu_update_state_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_GPU);

}
DECLARE_DEFERRED(pd_gpu_update_state_deferred);

void update_power_state_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_UPDATE_PWRSTAT);
}

void cypd_enable_interrupt(int controller, int enable_ndisable)
{
	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	if (enable_ndisable)
		gpio_enable_interrupt(pd_chip_config[controller].gpio);
	else
		gpio_disable_interrupt(pd_chip_config[controller].gpio);
}

static void cypd_print_version(int controller, const char *vtype, uint8_t *data)
{
	/*
	 * Base version: Cypress release version
	 * Application version: FAE release version
	 */
	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	CPRINTS("Controller %d  %s version B:%X.%X.%X.%X , AP:%X.%X.%X",
		controller, vtype,
		(data[3]>>4) & 0xF, (data[3]) & 0xF, data[2], data[0] + (data[1]<<8),
		(data[7]>>4) & 0xF, (data[7]) & 0xF, data[6]);
}

static void cypd_get_version(int controller)
{
	int rv;
	int i;
	uint8_t data[24] = {0};
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	rv = i2c_read_offset16_block(i2c_port, addr_flags, CCG_READ_ALL_VERSION_REG, data, 24);
	if (rv != EC_SUCCESS)
		CPRINTS("READ_ALL_VERSION_REG failed");

	cypd_print_version(controller, "App1", data+8);
	cypd_print_version(controller, "App2", data+16);

	/* store the FW2 version into pd_chip_info struct */
	for (i = 0; i < 8; i++)
		pd_chip_config[controller].version[i] = data[16+i];
}

static void pdo_init_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_CHANGE_P0_PDO_LIST);
}
DECLARE_DEFERRED(pdo_init_deferred);

static void cypd_changing_source_pdo_list(int controller, int port, int profile)
{
	int fail_step;

#ifndef CONFIG_SELECT_3A_TYPEC_OUTPUT_CURRENT
	return;
#endif

	/*
	 * EC needs to provide the data for all Source PDOs when doing a dynamic update of the PDOs.
	 * If less than 7 PDOs are required, the remaining PDO values should be set to 0.
	 */
	uint8_t pdos_reg[32] = {
			0x50, 0x43, 0x52, 0x53,	/* “SRCP”		*/
			0x5A, 0x90, 0x01, 0x27,	/* PDO0 - 0.9A	*/
			0x96, 0x90, 0x01, 0x27,	/* PDO1 - 1.5A	*/
			0x2c, 0x91, 0x01, 0x27,	/* PDO2 - 3A	*/
			0x00, 0x00, 0x00, 0x00,	/* PDO3			*/
			0x00, 0x00, 0x00, 0x00,	/* PDO4			*/
			0x00, 0x00, 0x00, 0x00,	/* PDO5			*/
			0x00, 0x00, 0x00, 0x00	/* PDO6			*/
		};

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	if (cypd_write_reg_block(controller, CCG_WRITE_DATA_MEMORY_REG(port, 0),
			pdos_reg, sizeof(pdos_reg))) {
		fail_step = 1;
		goto change_pdo_list_fail;
	}

	/**
	 * When the SELECT_SOURCE_PDO register is written, CCG will check the write data area for
	 * a valid Source PDO signature (“SRCP”). If the valid signature is found, CCG will copy
	 * the 28 bytes of Source PDO data from bytes 4 to 31 of the write data area into the
	 * active Source PDO set
	 */
	if (cypd_write_reg8_wait_ack(controller, CCG_SELECT_SOURCE_PDO_REG(port), BIT(profile))) {
		fail_step = 2;
		goto change_pdo_list_fail;
	}

	memset(pdos_reg, 0, sizeof(pdos_reg));

	/* Clear Signature “SRCP” for PDO update finish */
	if (cypd_write_reg_block(controller, CCG_WRITE_DATA_MEMORY_REG(port, 0),
			pdos_reg, sizeof(pdos_reg))) {
		fail_step = 3;
		goto change_pdo_list_fail;
	}

change_pdo_list_fail:
	if (fail_step)
		CPRINTS("CYPD: change PDOs list fail at step %d", fail_step);
}

static int cypd_select_rp(int port, uint8_t profile)
{
	int rv, resp_code;

	if (verbose_msg_logging)
		CPRINTS("Set typec port %d to profile:%d", port, profile);

	rv = cypd_write_reg_with_respond(PORT_TO_CONTROLLER(port),
			CCG_PD_CONTROL_REG(PORT_TO_CONTROLLER_PORT(port)),
			profile, &resp_code);
	if (rv != EC_SUCCESS)
		CPRINTS("SET TYPEC RP failed");

	return rv;
}

static int cypd_select_pdo(int controller, int port, uint8_t profile)
{
	int rv, resp_code;

	if (verbose_msg_logging)
		CPRINTS("Set typec port %d to PDO:%d", PDPORT(controller, port), profile);

	rv = cypd_write_reg_with_respond(controller, CCG_SELECT_SOURCE_PDO_REG(port),
		BIT(profile), &resp_code);
	if (rv != EC_SUCCESS)
		CPRINTS("SET CCG_SELECT_REG failed");

	return rv;
}

void cypd_evaluate_port_profile(int controller, int port, int ccg_event)
{
	int pd_port = PDPORT(controller, port);
	int shared_pd_port = PDPORT(controller, (port ? 0 : 1));
	bool first_3a_port = false;
	bool first_1p5a_port = false;
	bool restore_profile = false;
	bool profile_is_changed = false;
	static uint8_t ignore_evaluate_reason;
	bool allow_profile_swap  = true;

#ifndef CONFIG_SELECT_3A_TYPEC_OUTPUT_CURRENT
	return
#endif

	/* Skip to evaluate the port profile if the PD chip only one type-c port */
	if (pd_chip_config[controller].support_max_port < 2)
		return;

	/* Skip to evaluate the port profile if the safety level is 2 or more */
	if (pre_safety_level >= TYPEC_SAFETY_LEVEL_2)
		return;

	/* Skip to evaluate the port profile if both are source ports */
	if (pd_port_states[pd_port].c_state == CCG_STATUS_SOURCE &&
		pd_port_states[shared_pd_port].c_state == CCG_STATUS_SOURCE)
		return;

	if (ccg_event == CCG_RESPONSE_PORT_DISCONNECT ||
		pd_port_states[pd_port].c_state == CCG_STATUS_SOURCE) {
		if (pd_port_states[shared_pd_port].c_state == CCG_STATUS_NOTHING)
			restore_profile = true;

		/* Only allow swapping profiles when both ports sink devices */
		allow_profile_swap  = false;
	} else {
		/* Avoid the infinite loop if the ports reset or exchange profile */
		if (ignore_evaluate_reason & BIT(controller))
			return;

		if (pd_port_states[pd_port].c_state == CCG_STATUS_SINK) {

			if (pd_port_states[pd_port].max_operating_current <= 1500 &&
			    pd_port_states[pd_port].max_operating_current > 0 &&
			    pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0]
				!= CCG_PD_CMD_SET_TYPEC_1_5A) {
				/* Current PD device maximum operating current <= 1.5A */
				first_1p5a_port = true;
			} else if (pd_port_states[shared_pd_port].c_state != CCG_STATUS_SINK ||
			    (!pd_port_states[shared_pd_port].pd_state &&
			    pd_port_states[shared_pd_port].current < 3000))
				first_3a_port = true;
		}
	}

	/* Shared 3A type-c port with the one PD chip */
	if (first_3a_port) {

		/* Non-PD device and the Rp value is 1.5A or 0.9A */
		if ((!pd_port_states[pd_port].pd_state && pd_port_states[pd_port].current != 3000)
		    || pd_port_states[pd_port].safety_table[TYPEC_SAFETY_LEVEL_0] ==
		    CCG_PD_CMD_SET_TYPEC_1_5A)
			return;

		/* Override the safety table if the current should reduce to 1.5A */
		for (int level = 0; level < TYPEC_SAFETY_LEVEL_2; level++) {

			if (pd_port_states[shared_pd_port].safety_table[level] ==
			    CCG_PD_CMD_SET_TYPEC_3A) {
				pd_port_states[shared_pd_port].safety_table[level] =
					CCG_PD_CMD_SET_TYPEC_1_5A;
				profile_is_changed = true;
			}
		}

		if (profile_is_changed) {
			k_msleep(100);
#ifndef CONFIG_CHIPSET_INTEL
			/* Reduce the typec Rp value and PDO current to 1.5A */
			if ((pd_to_ucsi_port(shared_pd_port) == UCSI_PORT_1 ||
				pd_to_ucsi_port(shared_pd_port) == UCSI_PORT_4)) {
				cypd_select_rp(shared_pd_port,	CCG_PD_CMD_SET_TYPEC_1_5A);
			}
#endif
			cypd_select_pdo(controller, (port ? 0 : 1), CCG_PD_CMD_SET_TYPEC_1_5A);
		}
	} else if (first_1p5a_port) {

		/* If shared port already selects the 1.5A, ignore to change PDO */
		if (pd_port_states[shared_pd_port].c_state == CCG_STATUS_SINK &&
		    pd_port_states[shared_pd_port].safety_table[TYPEC_SAFETY_LEVEL_0] ==
		    CCG_PD_CMD_SET_TYPEC_1_5A)
			return;

		for (int level = 0; level < TYPEC_SAFETY_LEVEL_2; level++) {
			pd_port_states[pd_port].safety_table[level] = CCG_PD_CMD_SET_TYPEC_1_5A;
			/* Ensure shared PD ports perform 3A profiles */
			pd_port_states[shared_pd_port].safety_table[level] =
				CCG_PD_CMD_SET_TYPEC_3A;
		}

		k_msleep(100);
		cypd_select_pdo(controller, port, CCG_PD_CMD_SET_TYPEC_1_5A);
		/* Wait for the first port */
		k_msleep(100);
		cypd_select_pdo(controller, PORT_TO_CONTROLLER_PORT(shared_pd_port),
			CCG_PD_CMD_SET_TYPEC_3A);
	} else if (restore_profile) {

		for (int idx = 0; idx < pd_chip_config[controller].support_max_port; idx++) {
			int port_idx = PDPORT(controller, idx);

			for (int level = 0; level < TYPEC_SAFETY_LEVEL_2; level++) {
				int profile = pd_port_states[port_idx].safety_table[level];

				if (profile == CCG_PD_CMD_SET_TYPEC_1_5A) {
					pd_port_states[port_idx].safety_table[level] =
						CCG_PD_CMD_SET_TYPEC_3A;
					profile_is_changed = true;
				}
			}

			if (profile_is_changed) {
#ifndef CONFIG_CHIPSET_INTEL
				/* Restore the typec Rp value and PDO current to 3A */
				if (pd_to_ucsi_port(port_idx) == UCSI_PORT_1 ||
					pd_to_ucsi_port(port_idx) == UCSI_PORT_4) {
					cypd_select_rp(port_idx, CCG_PD_CMD_SET_TYPEC_3A);
				}
#endif
				cypd_select_pdo(controller, idx, CCG_PD_CMD_SET_TYPEC_3A);
			}
		}

		ignore_evaluate_reason &= ~(BIT(controller));
	} else {
		if ((pd_port_states[pd_port].safety_table[pre_safety_level] ==
		     pd_port_states[shared_pd_port].safety_table[pre_safety_level]) &&
		    (pd_port_states[pd_port].safety_table[pre_safety_level] ==
		     CCG_PD_CMD_SET_TYPEC_3A)) {
			int target_port = controller ?
				ucsi_to_pd_port(UCSI_PORT_3) : ucsi_to_pd_port(UCSI_PORT_2);

			/* Force reduce port 2 or port5 to 1.5A after resetting the pd ports */
			for (int level = 0; level < TYPEC_SAFETY_LEVEL_2; level++) {
				pd_port_states[target_port].safety_table[level] =
					CCG_PD_CMD_SET_TYPEC_1_5A;
				profile_is_changed = true;
			}

			if (profile_is_changed) {
				k_msleep(100);
				cypd_select_pdo(controller, PORT_TO_CONTROLLER_PORT(target_port),
					CCG_PD_CMD_SET_TYPEC_1_5A);
				ignore_evaluate_reason |= BIT(controller);
			}
		} else if ((pre_safety_level < TYPEC_SAFETY_LEVEL_2) &&
		    ((pd_port_states[shared_pd_port].pd_state) &&
		    (pd_port_states[shared_pd_port].current <= 1500) &&
			(pd_port_states[shared_pd_port].safety_table[pre_safety_level] ==
		     CCG_PD_CMD_SET_TYPEC_3A)) &&
		    (((pd_port_states[pd_port].pd_state) &&
		    (pd_port_states[pd_port].max_operating_current > 1500)) ||
		     pd_port_states[pd_port].rdo_mismatch) && allow_profile_swap) {
			/**
			 * Another port maximum operating current less than 1.5A
			 * EC allows PD chip provide more current if the RDO capabilities mismatch
			 * flag is set or the maximum operating current is 3A.
			 */
			CPRINTS("Allow port%d requests more power.", pd_port);

			/* Exchange the typec profile */
			for (int level = 0; level < TYPEC_SAFETY_LEVEL_2; level++) {
				int temp;

				temp = pd_port_states[pd_port].safety_table[level];
				pd_port_states[pd_port].safety_table[level] =
					pd_port_states[shared_pd_port].safety_table[level];
				pd_port_states[shared_pd_port].safety_table[level] = temp;
				profile_is_changed = true;
			}

			if (profile_is_changed) {
				int pd_port_profile =
				  pd_port_states[pd_port].safety_table[pre_safety_level];
				int shared_pd_port_profile =
				  pd_port_states[shared_pd_port].safety_table[pre_safety_level];

				cypd_select_pdo(controller, port, pd_port_profile);
				/* Wait for the first port */
				k_msleep(100);
				cypd_select_pdo(controller, (port ? 0 : 1),
					shared_pd_port_profile);

				/**
				 * Exchange the profile, ignore the next negotiation until
				 * all port is disconnected
				 */
				ignore_evaluate_reason |= BIT(controller);
			}
		}
	}
}

void cypd_update_safety_table(int safety_level)
{
	if (pre_safety_level != safety_level) {
		for (int port = 0; port < PD_PORT_COUNT; port++) {
			struct pd_port_current_state_t states = pd_port_states[port];
			int pre_profile = states.safety_table[pre_safety_level];
			int profile = states.safety_table[safety_level];

			if (pd_chip_config[PORT_TO_CONTROLLER(port)].state != CCG_STATE_READY)
				continue;

			if (pre_profile != profile) {
				cypd_select_pdo(
					PORT_TO_CONTROLLER(port),
					PORT_TO_CONTROLLER_PORT(port),
					profile);
			}
		}

		pre_safety_level = safety_level;
	}
}

static int pd_3a_flag;
static int pd_3a_set;
static int pd_3a_controller;
static int pd_3a_port;
static int first_3a_controller;
static int first_3a_port;
static int first_port_idx;
static int pd_ports_1_5A_flag[PD_PORT_COUNT];
static int rdo_3a_flag[PD_PORT_COUNT];

int cypd_port_3a_status(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	if (pd_3a_flag &&
		controller == pd_3a_controller &&
		port_idx == pd_3a_port)
		return true;
	return false;
}

int cypd_port_3a_set(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	if (pd_3a_set)
		return false;

	pd_3a_set = 1;
	pd_3a_flag = 1;
	pd_3a_controller = controller;
	pd_3a_port = port_idx;

	return true;
}

void cypd_port_3a_change(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	pd_3a_set = 1;
	pd_3a_flag = 1;
	pd_3a_controller = controller;
	pd_3a_port = port_idx;
}

void cypd_port_1_5a_set(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	pd_ports_1_5A_flag[port_idx] = 1;
}

int cypd_port_force_3A(int controller, int port)
{
	int port_idx = (controller << 1) + port;
	int port_1_5A_idx = 0;
	int i;
	for (i = 0; i < PD_PORT_COUNT; i++) {
		port_1_5A_idx += pd_ports_1_5A_flag[i];
	}

	/*
	 * Use of GRL verify test, when connect
	 * multi 1.5A device we should force last device to 3A.
	 */
	if (port_1_5A_idx >= (PD_PORT_COUNT - 1)) {
		if (!pd_ports_1_5A_flag[port_idx])
			return true;
	}
	return false;
}

void cypd_release_port(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	/* if port disconnect should set RP and PDO to default */

#ifndef CONFIG_SELECT_3A_TYPEC_OUTPUT_CURRENT
	return;
#endif
	cypd_select_rp(port_idx, CCG_PD_CMD_SET_TYPEC_1_5A);
	cypd_select_pdo(controller, port, CCG_PD_CMD_SET_TYPEC_3A);

	if (cypd_port_3a_status(controller, port)) {
		pd_3a_set = 0;
		pd_3a_flag = 0;
	}
	pd_ports_1_5A_flag[port_idx] = 0;
	rdo_3a_flag[port_idx] = 0;
}

void cypd_clear_port(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (cypd_port_3a_status(controller, port)) {
		pd_3a_set = 0;
		pd_3a_flag = 0;
	}
	pd_ports_1_5A_flag[port_idx] = 0;
	rdo_3a_flag[port_idx] = 0;
}

/*
 * function for profile check, if profile not change
 * don't send again.
 */
int cypd_profile_check(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	return pd_ports_1_5A_flag[port_idx] != 0;
}

static void pdo_c0p0_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C0P0);
}
DECLARE_DEFERRED(pdo_c0p0_deferred);

static void pdo_c0p1_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C0P1);
}
DECLARE_DEFERRED(pdo_c0p1_deferred);

static void pdo_c1p0_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C1P0);
}
DECLARE_DEFERRED(pdo_c1p0_deferred);

static void pdo_c1p1_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C1P1);
}
DECLARE_DEFERRED(pdo_c1p1_deferred);

static void pdo_c2p0_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_C2P0);
}
DECLARE_DEFERRED(pdo_c2p0_deferred);

static void cypd_set_prepare_pdo(int controller, int port)
{
	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	switch (controller) {
	case PD_CHIP_0:
		if (!port)
			hook_call_deferred(&pdo_c0p0_deferred_data, 2000 * MSEC);
		else
			hook_call_deferred(&pdo_c0p1_deferred_data, 2100 * MSEC);
		break;
	case PD_CHIP_1:
		if (!port)
			hook_call_deferred(&pdo_c1p0_deferred_data, 2000 * MSEC);
		else
			hook_call_deferred(&pdo_c1p1_deferred_data, 2100 * MSEC);
		break;
	case PD_CHIP_GPU:
		if (!port)
			hook_call_deferred(&pdo_c2p0_deferred_data, 2100 * MSEC);
		break;
	}
}

static int cypd_modify_profile(int controller, int port, int profile)
{
	int rv;
	int port_idx = (controller << 1) + port;
	static const char * const current_level[] = {"0.9A", "1.5A", "3A", "InvA"};

	if (verbose_msg_logging)
		CPRINTS("PD Select PDO %s ", current_level[profile]);

	if (profile != CCG_PD_CMD_SET_TYPEC_1_5A) {
		rv = cypd_select_rp(port_idx, profile);
		if (rv != EC_SUCCESS)
			return rv;
	}

	rv = cypd_select_pdo(controller, port, profile);
	if (rv != EC_SUCCESS) {
		CPRINTS("PD Select PDO %s failed", current_level[profile]);
		cypd_clear_port(controller, port);
		cypd_set_prepare_pdo(controller, port);
		return rv;
	}

	/* Lock 1.5A port  */
	if (profile == CCG_PD_CMD_SET_TYPEC_1_5A)
		cypd_port_1_5a_set(controller, port);

	return EC_SUCCESS;
}

int cypd_modify_safety_power(int controller, int port, int profile)
{
	int rv;
	int port_idx = (controller << 1) + port;

	if (verbose_msg_logging)
		CPRINTS("PD Select PDO %s ", profile & 0x02 ? "3A" : "1.5A");

	rv = cypd_select_rp(port_idx, profile);
	rv = cypd_select_pdo(controller, port, profile);
	if (rv != EC_SUCCESS) {
		CPRINTS("PD Select PDO %s failed", profile & 0x02 ? "3A" : "1.5A");
		cypd_clear_port(controller, port);
		cypd_set_prepare_pdo(controller, port);
		return rv;
	}

	return EC_SUCCESS;
}

void cypd_set_typec_profile(int controller, int port)
{
	int rv;
	uint8_t pd_status_reg[4] = {0};
	uint8_t rdo_reg[4] = {0};

	int rdo_max_current = 0;
	int rdo_3a_idx = 0;
	int port_idx = (controller << 1) + port;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

#ifndef CONFIG_SELECT_3A_TYPEC_OUTPUT_CURRENT
	return;
#endif

	rv = cypd_read_reg_block(controller, CCG_PD_STATUS_REG(port), pd_status_reg, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_PD_STATUS_REG failed");

	/*do we have a valid PD contract*/
	pd_port_states[port_idx].pd_state = pd_status_reg[1] & BIT(2) ? 1 : 0;
	pd_port_states[port_idx].power_role =
			pd_status_reg[1] & BIT(0) ? PD_ROLE_SOURCE : PD_ROLE_SINK;

	if (pd_port_states[port_idx].power_role == PD_ROLE_SOURCE) {
		if (pd_port_states[port_idx].pd_state) {

#ifdef CONFIG_PLATFORM_EC_FRAMEWORK_LAPTOP_16
			/*
			 * If safety level(LEVEL_TYPEC_1_5A) is triggered,
			 * force 1.5A pdo to device.
			 */
			if (safety_force_typec_1_5A()) {
				rv = cypd_modify_profile(controller, port,
							CCG_PD_CMD_SET_TYPEC_1_5A);
				return;
			}
#endif
			/*
			 * first time set 3A PDO to device
			 * when device request RDO <= 1.5A
			 * resend 1.5A pdo to device
			 */

			cypd_read_reg_block(controller, CCG_CURRENT_RDO_REG(port), rdo_reg, 4);
			rdo_max_current = (((rdo_reg[1]>>2) + (rdo_reg[2]<<6)) & 0x3FF)*10;
			if (rdo_max_current > 1500) {
				rdo_3a_flag[port_idx] = 1;
				for (int i = 0; i < PD_PORT_COUNT; i++) {
					rdo_3a_idx += rdo_3a_flag[i];
				}
			}

			/* The first device force 3A PDO */
			if (!pd_3a_flag && cypd_port_3a_set(controller, port)) {
				rv = cypd_modify_profile(controller, port,
						CCG_PD_CMD_SET_TYPEC_3A);
				first_3a_controller = controller;
				first_3a_port = port;
				first_port_idx = (controller << 1) + port;
			/* Another device requires 3A, and the first device can drop to 1.5A */
			} else if (rdo_3a_flag[port_idx] && rdo_3a_idx == 1) {
				if (first_port_idx == port_idx)
					return;
				rv = cypd_modify_profile(controller, port,
						CCG_PD_CMD_SET_TYPEC_3A);
				cypd_select_rp(first_port_idx, CCG_PD_CMD_SET_TYPEC_1_5A);
				rv = cypd_modify_profile(first_3a_controller, first_3a_port,
						CCG_PD_CMD_SET_TYPEC_1_5A);
				cypd_port_3a_change(controller, port);
			} else if ((cypd_port_force_3A(controller, port) && !pd_3a_flag) ||
				cypd_port_3a_status(controller, port)) {
				if (!cypd_port_3a_set(controller, port))
					return;
				rv = cypd_modify_profile(controller, port,
						CCG_PD_CMD_SET_TYPEC_3A);
			} else if (!cypd_port_3a_status(controller, port))
				rv = cypd_modify_profile(controller, port,
						CCG_PD_CMD_SET_TYPEC_1_5A);
		} else {
			cypd_write_reg8(controller, CCG_PD_CONTROL_REG(port),
				CCG_PD_CMD_SET_TYPEC_1_5A);
		}
	}
}

void cypd_port_current_setting(void)
{
	for (int i = 0; i < PD_CHIP_COUNT; i++) {
		cypd_set_prepare_pdo(i, 0);
		cypd_set_prepare_pdo(i, 1);
	}
}

static void cypd_pdo_reset_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_PDO_RESET);
}
DECLARE_DEFERRED(cypd_pdo_reset_deferred);

static void cypd_ppm_port_clear(void)
{
	memset(pd_ports_1_5A_flag, 0, sizeof(pd_ports_1_5A_flag));
	pd_3a_set = 0;

	/* need init PDO again because PD chip will clear PDO data */
	hook_call_deferred(&pdo_init_deferred_data, 1);
}

/*
 * send a message using DM_CONTROL to port partner
 * pd_header is using chromium PD header with upper bits defining SOP type
 * pd30 is set for batttery status messages
 * response timer is set to false for messages that are a response
 * data includes
 * pd header bytes 0 -1
 * message, or extmessage header - then data
 * length should include length of all data after pd header
 */
void cypd_send_msg(int controller, int port, uint32_t pd_header, uint16_t ext_hdr,
	bool pd30, bool response_timer, void *data, uint32_t data_size)
{
	uint16_t header[2] = {0};
	uint16_t dm_control_data;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	/**
	 * The extended message data should be written to the write data memory
	 * in the following format:
	 * Byte 0 : Message type [4:0]
	 * Byte 1 : Reserved
	 * Byte 3 - 2 : Extended message header
	 * Byte N - 4 : data
	 */

	header[0] = pd_header;
	header[1] = ext_hdr;

	cypd_write_reg_block(controller, CCG_WRITE_DATA_MEMORY_REG(port, 0),
		(void *)header, 4);

	cypd_write_reg_block(controller, CCG_WRITE_DATA_MEMORY_REG(port, 4),
		data, data_size);

	/**
	 * The DM_CONTROL register should then be written to in the following format:
	 * Byte 0
	 *	- BIT 1 - 0 : Packet type should be set to SOP(0), SOP'(1), or SOP''(2).
	 *	- BIT 2 : PD 3.0 Message bit (Bit 2) should be clear.
	 *	- BIT 3 : Extended message bit (Bit 3) should be set.
	 *	- BIT 4 : Respoonse timer disable bit should be set as desired.
	 * Byte 1 : The data length specified here will be the actual length of data
	 *			written into the write data memory, inclusive of the 4 byte header
	 *
	 * TODO: Need to process chunk extended message [4:32]
	 */
	dm_control_data = PD_HEADER_GET_SOP(pd_header);
	if (ext_hdr)
		dm_control_data |= CCG_DM_CTRL_EXTENDED_DATA_REQUEST;
	if (pd30)
		dm_control_data |= CCG_DM_CTRL_PD3_DATA_REQUEST;
	if (!response_timer)
		dm_control_data |= CCG_DM_CTRL_SENDER_RESPONSE_TIMER_DISABLE;
	dm_control_data += ((data_size + 4) << 8);

	cypd_write_reg16(controller, CCG_DM_CONTROL_REG(port), dm_control_data);
}

int cypd_handle_extend_msg(int controller, int port, int len, enum tcpci_msg_type sop_type)
{
	/**
	 * Extended Message Received Events
	 * Event Code = 0xAC(SOP), 0xB4(SOP'), 0xB5(SOP'')
	 * Event length = 4 + Extended message length
	 */

	/*Todo handle full length Extended messages up to 260 bytes*/
	int type;
	int rv;
	int i;
	int port_idx = (controller << 1) + port;
	int pd_header;

	if (len > 260) {
		CPRINTS("ExtMsg Too Long");
		return EC_ERROR_INVAL;
	}

	/* Read the extended message packet */
	rv = cypd_read_reg_block(controller,
		CCG_READ_DATA_MEMORY_REG(port, 0), (void *)&(rx_emsg[port_idx].len), len);
		/*
		 * avoid a memcopy so direct copy into the buffer and then swap header and len
		 * look at the memory layout for the rx_emsg structure to see why we do this
		 */
	rx_emsg[port_idx].header = rx_emsg[port_idx].len >> 16;
	pd_header = (rx_emsg[port_idx].len & 0xFFFF) + PD_HEADER_SOP(sop_type);
	rx_emsg[port_idx].len = len-4;

	/* Extended field shall be set to 1*/
	if (!PD_HEADER_EXT(pd_header))
		return EC_ERROR_INVAL;

	type = PD_HEADER_TYPE(pd_header);

	switch (type) {
	case PD_EXT_CONTROL:
		/*
		 * Workaround to handle the message type is EPR KeepAlive.
		 * Note on the new PD firmware, it will pass the EPR keepalive messages
		 * to the EC right now. To avoid spamming the ec log until we figure out
		 * how to disable this.
		 */
		if (rx_emsg[port_idx].buf[0] == PD_EXT_CTRL_EPR_KEEPALIVE_ACK)
			break;
		__fallthrough;
	default:
		CPRINTF("Port:%d Unknown data type: 0x%02x Hdr:0x%04x ExtHdr:0x%04x Data:0x",
				port_idx, type, pd_header, rx_emsg[port_idx].header);
		for (i = 0; i < rx_emsg[port_idx].len; i++) {
			CPRINTF("%02x", rx_emsg[port_idx].buf[i]);
		}
		CPRINTF("\n");
		rv = EC_ERROR_INVAL;
		break;
	}

	return rv;
}

static void clear_port_state(int controller, int port)
{
	int port_idx = (controller << 1) + port;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	pd_port_states[port_idx].pd_state = 0; /*do we have a valid PD contract*/
	pd_port_states[port_idx].power_role = PD_ROLE_SINK;
	pd_port_states[port_idx].data_role = PD_ROLE_UFP;
	pd_port_states[port_idx].vconn = PD_ROLE_VCONN_OFF;
	pd_port_states[port_idx].epr_active = 0;
	pd_port_states[port_idx].epr_support = 0;
	pd_port_states[port_idx].epr_retry_count = 0;
	pd_port_states[port_idx].cc = POLARITY_CC1;
	pd_port_states[port_idx].c_state = 0;
	pd_port_states[port_idx].current = 0;
	pd_port_states[port_idx].voltage = 0;
	pd_port_states[port_idx].max_operating_current = 0;
}

#ifdef CONFIG_CHARGER_HAS_VOLTAGE_REGULATOR
static int64_t calculate_input_current(int pd_current, int pd_voltage, int scale_voltage,
	int factor1, int factor2, int low_voltage_factor)
{
	if (pd_voltage > 20000) {
		return (int64_t)pd_current * (int64_t)pd_voltage * factor1 * factor2
				/ scale_voltage;
	} else {
		return (int64_t)pd_current * low_voltage_factor / 100;
	}
}
#endif

void cypd_update_port_state(int controller, int port)
{
	int rv;
	uint8_t pd_status_reg[4];
	uint32_t pdo_reg;
	uint8_t rdo_reg[4];
	int typec_status_reg;
	int pd_current = 0;
	int pd_voltage = 0;
	int rdo_operating_current = 0;
	int rdo_max_operating_current = 0;
	bool capability_mismatch = false;
	int type_c_current = 0;
	int port_idx = (controller << 1) + port;
#ifdef CONFIG_CHARGER_HAS_VOLTAGE_REGULATOR
	int64_t calculate_ma;
	int level_buck_ma;
#endif

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	rv = cypd_read_reg_block(controller, CCG_PD_STATUS_REG(port), pd_status_reg, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("CCG_PD_STATUS_REG failed");
	pd_port_states[port_idx].pd_state =
		pd_status_reg[1] & BIT(2) ? 1 : 0; /*do we have a valid PD contract*/
	pd_port_states[port_idx].power_role =
		pd_status_reg[1] & BIT(0) ? PD_ROLE_SOURCE : PD_ROLE_SINK;
	pd_port_states[port_idx].data_role =
		pd_status_reg[0] & BIT(6) ? PD_ROLE_DFP : PD_ROLE_UFP;
	pd_port_states[port_idx].vconn =
		pd_status_reg[1] & BIT(5) ? PD_ROLE_VCONN_SRC : PD_ROLE_VCONN_OFF;
	if (pd_port_states[port_idx].epr_active != 0xff)
		pd_port_states[port_idx].epr_active = pd_status_reg[2] & BIT(7) ? 1 : 0;

	rv = cypd_read_reg8(controller, CCG_TYPE_C_STATUS_REG(port), &typec_status_reg);
	if (rv != EC_SUCCESS)
		CPRINTS("CCG_TYPE_C_STATUS_REG failed");

	pd_port_states[port_idx].cc = typec_status_reg & BIT(1) ? POLARITY_CC2 : POLARITY_CC1;
	pd_port_states[port_idx].c_state = (typec_status_reg >> 2) & 0x7;
	switch ((typec_status_reg >> 6) & 0x03) {
	case 0:
		type_c_current = 900;
		break;
	case 1:
		type_c_current = 1500;
		break;
	case 2:
		type_c_current = 3000;
		break;
	}
#ifdef CONFIG_BOARD_LOTUS
	update_external_cc_mux(port_idx,pd_port_states[port_idx].c_state == CCG_STATUS_NOTHING ? 0xFF : pd_port_states[port_idx].cc);
#endif

	rv = cypd_read_reg_block(controller, CCG_CURRENT_PDO_REG(port), &pdo_reg, 4);
	switch (pdo_reg & PDO_TYPE_MASK) {
		case PDO_TYPE_FIXED:
			pd_current = PDO_FIXED_CURRENT(pdo_reg);
			pd_voltage = PDO_FIXED_VOLTAGE(pdo_reg);
			break;
		case PDO_TYPE_BATTERY:
			pd_current = PDO_BATT_MAX_POWER(pdo_reg)/PDO_BATT_MAX_VOLTAGE(pdo_reg);
			pd_voltage = PDO_BATT_MIN_VOLTAGE(pdo_reg);
		break;
		case PDO_TYPE_VARIABLE:
			pd_current = PDO_VAR_MAX_CURRENT(pdo_reg);
			pd_voltage = PDO_VAR_MAX_VOLTAGE(pdo_reg);
		break;
		case PDO_TYPE_AUGMENTED:
			pd_current = PDO_AUG_MAX_CURRENT(pdo_reg);
			pd_voltage = PDO_AUG_MAX_VOLTAGE(pdo_reg);
		break;
	}

	cypd_read_reg_block(controller, CCG_CURRENT_RDO_REG(port), rdo_reg, 4);
	rdo_operating_current = (((rdo_reg[1] >> 2) + (rdo_reg[2] << 6)) & 0x3FF) * 10;
	rdo_max_operating_current = ((rdo_reg[0] + (rdo_reg[1] << 8)) & 0x3FF) * 10;
	capability_mismatch = (rdo_reg[3] >> 3) & 0x01;

	/*
	 * The port can have several states active:
	 * 1. Type C active (with no PD contract) CC resistor negociation only
	 * 2. Type C active with PD contract
	 * 3. Not active
	 * Each of 1 and 2 can be either source or sink
	 */

	if (pd_port_states[port_idx].c_state == CCG_STATUS_SOURCE) {
		/*
		 * at GRL TEST.PD.PS.SRC.3#18 if device didn't provide current(5V0A)
		 * DUT should not get power from device, so limit the charger
		 * to avoid drawn the current from device
		 */
		if (pd_current == 0)
			type_c_current = pd_current;

		if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
			typec_set_input_current_limit(port_idx, type_c_current, TYPE_C_VOLTAGE);
			charge_manager_set_ceil(port_idx, CEIL_REQUESTOR_PD,
								type_c_current);
		}
		pd_port_states[port_idx].current = type_c_current;
		pd_port_states[port_idx].voltage = TYPE_C_VOLTAGE;
	} else {
		if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
			typec_set_input_current_limit(port_idx, 0, 0);
			charge_manager_set_ceil(port,
				CEIL_REQUESTOR_PD,
				CHARGE_CEIL_NONE);
		}
	}
	if (pd_port_states[port_idx].c_state == CCG_STATUS_SINK) {
		pd_port_states[port_idx].current = type_c_current;
		pd_port_states[port_idx].voltage = TYPE_C_VOLTAGE;
	}

	if (pd_port_states[port_idx].c_state == CCG_STATUS_NOTHING) {
		pd_port_states[port_idx].current = 0;
		pd_port_states[port_idx].voltage = 0;
	}

	if (pd_port_states[port_idx].pd_state) {
		if (pd_port_states[port_idx].power_role == PD_ROLE_SINK) {
			if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
				pd_set_input_current_limit(port_idx, pd_current, pd_voltage);
				charge_manager_set_ceil(port_idx, CEIL_REQUESTOR_PD, pd_current);
			}
			pd_port_states[port_idx].current = pd_current;
			pd_port_states[port_idx].voltage = pd_voltage;
			if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGER_RAA489300) &&
				(battery_is_present() != BP_YES)) {
				board_level_buck_update();
			}
		} else {
			if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
				pd_set_input_current_limit(port_idx, 0, 0);
			}
			/*Source*/
			pd_port_states[port_idx].current = rdo_operating_current;
			pd_port_states[port_idx].max_operating_current = rdo_max_operating_current;
			pd_port_states[port_idx].voltage = TYPE_C_VOLTAGE;

		}
	} else {
		if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
			pd_set_input_current_limit(port_idx, 0, 0);
		}
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
		charge_manager_update_dualrole(port_idx, CAP_DEDICATED);
	}

#ifdef CONFIG_PD_CCG8_EPR
	if (!!(epr_progress_status() & EPR_PROCESS_MASK) &&
	    !(epr_progress_status() & ~EPR_PROCESS_MASK)) {

		if (get_active_charge_pd_port() == port_idx) {
			charger_discharge_on_ac(0);

#ifdef CONFIG_CHARGER_HAS_VOLTAGE_REGULATOR
#ifdef CONFIG_BOARD_LOTUS
			/**
			 * >20V: (charge_ma * charge_mv / 20000 ) * 0.9 * 0.94
			 * <=20V: (charge_ma * 88 / 100)
			 */
			calculate_ma = calculate_input_current(pd_current, pd_voltage,
								200000000, 90, 95, 88);
#elif defined(CONFIG_BOARD_TULIP)
			/**
			 * >20V: (charge_ma * charge_mv / 24000 ) * 0.95 * 0.95
			 * <=20V: (charge_ma * 98 / 100)
			 */
			calculate_ma = calculate_input_current(pd_current, pd_voltage,
								240000000, 95, 95, 98);
			level_buck_ma = pd_current * 98 / 100;
#endif
			if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGER_RAA489300)) {
				level_buck_set_input_current_limit(level_buck_ma);
			}

			charger_set_input_current_limit(0, (int)calculate_ma);
#endif
		}

		clear_epr_progress_mask();
	}
#endif

#ifdef CONFIG_PD_CHIP_CCG6
	/**
	 * CCG6 use the external mux to switch the SUB and UART signal.
	 * Call from project to check the type-c port status to enable/disable ccd mode
	 */
	cypd_ccd_mode_control();
#endif
}

void cypd_set_power_state(int power_state, int controller)
{
	int rv = EC_SUCCESS;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	rv = cypd_write_reg8_wait_ack(controller, CCG_SYS_PWR_STATE, power_state);
	if (rv != EC_SUCCESS) {
		CPRINTS("C%d, cypd set system power state 0x%02x failed, rv=%d",
			controller, power_state, rv);
	}
}

static void cypd_update_power_status(int controller)
{
	int rv = EC_SUCCESS;
	int power_status = 0;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

#ifdef CONFIG_PLATFORM_EC_BATTERY
	int pd_controller_is_sink = (prev_charge_port & 0x02) >> 1;
	bool battery_can_discharge = (battery_is_present() == BP_YES) &
		battery_get_disconnect_state();

	if (battery_can_discharge)
		power_status |= CCG_POWERSTAT_BATT_PRESENT;

	if ((extpower_is_present() && battery_can_discharge) ||
		(extpower_is_present() && controller != pd_controller_is_sink &&
		prev_charge_port >= 0))
		power_status |= CCG_POWERSTAT_EXT_POWER_PRESENT + CCG_POWERSTAT_EXT_POWER_TYPE;
#else
	power_status = CCG_POWERSTAT_INTERNAL_POWER;
#endif

	rv = cypd_write_reg8_wait_ack(controller, CCG_POWER_STAT, power_status);
	if (rv != EC_SUCCESS) {
		CPRINTS("C%d, cypd set power status 0x%02x failed, rv=%d",
			controller, power_status, rv);
	}
}

static void port_to_safe_mode(int port)
{
	uint8_t data[2] = {0x00, CCG_PD_USER_MUX_CONFIG_SAFE};

	data[0] = PORT_TO_CONTROLLER_PORT(port);
	cypd_write_reg_block(PORT_TO_CONTROLLER(port), CCG_MUX_CFG_REG, data, 2);
	cypd_write_reg_block(PORT_TO_CONTROLLER(port), CCG_DEINIT_PORT_REG, data, 1);
	CPRINTS("P%d: Safe", port);

}

void cypd_set_power_active(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_S_CHANGE);
}

void cypd_update_chips_state(int controller, enum ccg_pd_state state)
{
	pd_chip_config[controller].state = state;

	/* Shuold clear the type-c status and update charge_manager */
	if (state == CCG_STATE_NO_POWER) {
		for (int port = 0; port < pd_chip_config[controller].support_max_port; port++) {
			int port_idx = (controller << 1) + port;

			clear_port_state(controller, port);
			if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
				typec_set_input_current_limit(port_idx, 0, 0);
				charge_manager_set_ceil(port_idx, CEIL_REQUESTOR_PD,
					CHARGE_CEIL_NONE);
				charge_manager_update_dualrole(port_idx, CAP_UNKNOWN);
			}

			cypd_enable_interrupt(controller, 0);
		}
	}
}

__overridable void cypd_customize_app_setup(int controller)
{
	/*
	 * CCG Chip behavior is different,
	 * use this function to customize setting
	 */
}

static void pd_batt_init_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_BATT_UPDATE);
}
DECLARE_DEFERRED(pd_batt_init_deferred);
DECLARE_HOOK(HOOK_AC_CHANGE, pd_batt_init_deferred, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, pd_batt_init_deferred, HOOK_PRIO_DEFAULT);


static void cypd_handle_state(int controller)
{
	int data;
	int delay = 0;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	switch (pd_chip_config[controller].state) {
	case CCG_STATE_WAIT_STABLE:
		uint64_t timer = get_time().val;

		if (timer > CONFIG_PD_WAIT_STABLE_TIMER * MSEC)
			pd_chip_config[controller].state = CCG_STATE_POWER_ON;

		if (controller == 0) {
			hook_call_deferred(&pd0_update_state_deferred_data,
				CONFIG_PD_WAIT_STABLE_TIMER * MSEC);
		} else {
			hook_call_deferred(&pd1_update_state_deferred_data,
				CONFIG_PD_WAIT_STABLE_TIMER * MSEC);
		}
		break;
	case CCG_STATE_BOOTLOADER:
	case CCG_STATE_POWER_ON:
		/* poll to see if the controller has booted yet */
		if (cypd_read_reg8(controller, CCG_DEVICE_MODE, &data) == EC_SUCCESS) {
			if ((data & 0x03) == 0x00) {
				CPRINTS("CYPD %d is in bootloader 0x%04x", controller, data);
				delay = CCG_MAX_TBOOTWAIT_VALUE;
				pd_chip_config[controller].state = CCG_STATE_BOOTLOADER;
				if (cypd_read_reg16(controller, CCG_BOOT_MODE_REASON, &data)
						== EC_SUCCESS) {
					CPRINTS("CYPD bootloader reason 0x%02x", data);
				}

			} else
				pd_chip_config[controller].state = CCG_STATE_APP_SETUP;
		} else {
			CPRINTS("CYPD %d read device mode failed.", controller);
			pd_chip_config[controller].state = CCG_STATE_ERROR;
		}
		/*try again in a while*/
		if (delay) {
			if (controller == PD_CHIP_0)
				hook_call_deferred(&pd0_update_state_deferred_data, delay);
			else if (controller == PD_CHIP_1)
				hook_call_deferred(&pd1_update_state_deferred_data, delay);
			else if (controller == PD_CHIP_GPU)
				hook_call_deferred(&pd_gpu_update_state_deferred_data, delay);
		} else
			task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0 << controller);
		break;

	case CCG_STATE_APP_SETUP:
		gpio_disable_interrupt(pd_chip_config[controller].gpio);
		cypd_get_version(controller);
		cypd_update_power_status(controller);

		update_system_power_state(controller);
		cypd_setup(controller);

		cypd_customize_app_setup(controller);

		ucsi_startup(controller);

		CPRINTS("CYPD %d Ready!", controller);
		pd_chip_config[controller].state = CCG_STATE_READY;

		gpio_enable_interrupt(pd_chip_config[controller].gpio);

		/* After all PD chips initialize completely, and then update the state */
		if (cypd_controllers_are_ready()) {

			task_set_event(TASK_ID_CYPD, CCG_EVT_UPDATE_PORTSTATE);
#if defined(CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE) || defined(CONFIG_PD_CCG8_CUSTOMIZE_BATT_MESSAGE)
			hook_call_deferred(&pd_batt_init_deferred_data, 100 * MSEC);
#endif /* CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE || CONFIG_PD_CCG8_CUSTOMIZE_BATT_MESSAGE */
			hook_call_deferred(&pdo_init_deferred_data, 25 * MSEC);
		}
		break;
	case CCG_STATE_NO_POWER:
		CPRINTS("CYPD %d no power!", controller);
		break;
	default:
		CPRINTS("PD handle_state but in 0x%02x state!", pd_chip_config[controller].state);
		break;
	}

}

__overridable int board_perform_error_recovery_port(int port)
{
	return port;
}


#ifdef CONFIG_PD_COMMON_VBUS_CONTROL
static uint8_t pd_c_fet_active_port;

DECLARE_DEFERRED(update_power_state_deferred);

int cypd_cfet_vbus_control(int port, bool enable, bool ec_control)
{
	int rv;
	int pd_controller = PORT_TO_CONTROLLER(port);
	int pd_port = PORT_TO_CONTROLLER_PORT(port);
	int regval = (ec_control ? CCG_EC_VBUS_CTRL_EN : 0) |
				(enable ? CCG_EC_VBUS_CTRL_ON : 0);

	if (port < 0 || port >= PD_PORT_COUNT) {
		return EC_ERROR_INVAL;
	}

	/* TODO: may check the vbus control after init GPU PD */
	if (!cypd_contoller_is_powered(pd_controller))
		return EC_ERROR_NOT_POWERED;

	rv = cypd_write_reg8_wait_ack(pd_controller, CCG_PORT_VBUS_FET_CONTROL(pd_port),
		regval);
	if (rv != EC_SUCCESS)
		CPRINTS("%s:%d fail:%d", __func__, port, rv);

	if (enable)
		pd_c_fet_active_port |= BIT(port);
	else
		pd_c_fet_active_port &= ~BIT(port);

	return rv;
}

uint8_t cypd_get_cfet_status(void)
{
	return pd_c_fet_active_port;
}

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
int board_set_active_charge_port(int charge_port)
{
	int i;

	CPRINTS("%s port %d, prev:%d", __func__, charge_port, prev_charge_port);

	if (prev_charge_port == charge_port) {
		/* in the case of hard reset, we do not turn off the old
		 * port, but the PD will implicitly clear the port
		 * so we need to turn on the vbus control again.
		 */
		cypd_cfet_vbus_control(charge_port, true, true);
		return EC_SUCCESS;
	}


	if (prev_charge_port != -1 &&
		prev_charge_port != charge_port) {
		/* Turn off the previous charge port before turning on the next port */
		cypd_cfet_vbus_control(prev_charge_port, false, true);

		/**
		 * Multi-port switch, we should force set the current limit to the active
		 * port before enabling the sink path.
		 */
		if (charge_port != -1) {
			if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
				charger_set_input_current_limit(0,
						pd_port_states[charge_port].current);
			}
		}
	}

	for (i = 0; i < PD_PORT_COUNT; i++) {
		/* Just brute force all ports, we want to make sure
		 * we always update all ports in case a PD controller rebooted or some
		 * other error happens that we are not tracking state with.
		 */
		cypd_cfet_vbus_control(i, i == charge_port, true);
	}
	prev_charge_port = charge_port;
	hook_call_deferred(&update_power_state_deferred_data, 100 * MSEC);

	return EC_SUCCESS;
}
#endif /* CONFIG_PD_COMMON_VBUS_CONTROL */

static void print_pd_response_code(uint8_t controller, uint8_t port, uint8_t id, int len)
{
	if (verbose_msg_logging) {
		CPRINTS("PD Controller %d Port %d  Code 0x%02x %s Len: 0x%02x",
		controller,
		port,
		id,
		id & 0x80 ? "Response" : "Event",
		len);
	}
}




/*****************************************************************************/
/* Project */
int cypd_get_ac_power(void)
{
	int ac_power_mW = 0;

	/* -1 means no ac */
	if (prev_charge_port == -1)
		return 0;

	ac_power_mW = (pd_port_states[prev_charge_port].current
		* pd_port_states[prev_charge_port].voltage);

	return (ac_power_mW / 1000);
}

int cypd_get_active_port_voltage(void)
{
	if (prev_charge_port == -1)
		return 0;

	return pd_port_states[prev_charge_port].voltage;
}

int cypd_vsys_to_vbus_transition(int port)
{
	int rv;

	rv = cypd_write_reg8_wait_ack(PORT_TO_CONTROLLER(port),
				CCG_PD_CONTROL_REG(PORT_TO_CONTROLLER_PORT(port)),
				CCG_PD_CMD_VSYS_TO_VBUS);

	return rv;
}

/*****************************************************************************/
/* Interrupt handler */

int cypd_device_int(int controller)
{
	int data;

	if (cypd_read_reg16(controller, CCG_RESPONSE_REG, &data) == EC_SUCCESS) {

		print_pd_response_code(controller, -1, data & 0xff, data>>8);

		switch (data & 0xFF) {
		case CCG_RESPONSE_RESET_COMPLETE:
			CPRINTS("PD%d Reset Complete", controller);
#ifdef CONFIG_PD_CHIP_CCG6
			if (pd_chip_config[controller].state != CCG_STATE_WAIT_STABLE)
#endif
				pd_chip_config[controller].state = CCG_STATE_POWER_ON;

			/* Run state handler to set up controller */
			task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0 << controller);
			break;
		case CCG_RESPONSE_MESSAGE_QUEUE_OVERFLOW:
			CPRINTS("PD%d Message Overflow", controller);
			break;
#ifdef CONFIG_CHIPSET_AMD
		case CCG_RESPONSE_AMD_CROSSBAR_READY:
			/**
			 * Vendor requests EC to do the error recovery after
			 * AMD Crossbar is ready.
			 *
			 * They add the specific response code 0x38 to notify EC
			 * and then EC performs the typec error recovery
			 */
			CPRINTS("AMD Crossbar is ready");
			perform_error_recovery(controller);
			break;
#endif
		default:
			/* reduce the EC logs without debugging */
			if (verbose_msg_logging)
				CPRINTS("C%d device response: 0x%x", controller, data & 0xFF);
		}
	} else
		return EC_ERROR_INVAL;


	return EC_SUCCESS;
}
static bool pending_dp_poweroff[PD_PORT_COUNT];
static void poweroff_dp_check(void)
{
	int i;
	int alt_active = 0;

	for (i = 0; i < PD_PORT_COUNT; i++) {
		if (pending_dp_poweroff[i]) {
			/* see if alt mode is active */
			cypd_read_reg8(PORT_TO_CONTROLLER(i),
				CCG_DP_ALT_MODE_CONFIG_REG(PORT_TO_CONTROLLER_PORT(i)),
				&alt_active);
			/*
			 * DP_ALT should be on bit 1 always, but there is a bug
			 * in the PD stack that if a port does not have TBT mode
			 * enabled, it will shift the DP alt mode enable bit to
			 * bit 0. Since we only whitelist DP alt mode cards, just
			 * mask on both as a workaround.
			 */
			if ((alt_active & (BIT(1) + BIT(0))) == 0) {
				port_to_safe_mode(i);
			}
			pending_dp_poweroff[i] = 0;
		}
	}
}
static void poweroff_dp_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_DPALT_DISABLE);

}
DECLARE_DEFERRED(poweroff_dp_deferred);

struct framework_dp_ids {
	uint16_t vid;
	uint16_t pid;
} const cypd_altmode_ids[] = {
	{0x32AC, 0x0002}, /* HDMI expension card vid and pid */
	{0x32AC, 0x0003}, /* DP expension card vid and pid */
	{0x32AC, 0x000E}, /* 180w adapter vid and pid */
	{0x32AC, 0x0023}, /* Chicony 60w adapter vid and pid */
	{0x32AC, 0x000B}, /* Phihong 60w adapter vid and pid */
	{0x32AC, 0x0022}, /* Chicony 240w adapter vid and pid */
	{0x32AC, 0x002D}, /* 100w adapter vid and pid */
};
struct match_vdm_header {
	uint8_t idx;
	uint8_t val;
} const framework_vdm_hdr_match[] = {
	{0, 0x8f},
	/*{1, 0x52},*/
	{2, 0},
	{4, 0x41},
	/*{5, 0xa0},*/
	{6, 0x00},
	{7, 0xFF},
	/*{8, 0xAC}, Framework VID */
	/*{9, 0x32}, */
	/*{10, 0x00},*/
	/*{11, 0x6C}*/
};

void cypd_handle_vdm(int controller, int port, uint8_t *data, int len)
{
	/* parse vdm
	 * if we get a DP alt mode VDM that matches our
	 * HDMI or DP VID/PID we will start a timer
	 * to set the port mux to safe/isolate
	 * if we get a enter alt mode later on,
	 * we will cancel the timer so that PD can
	 * properly enter the alt mode
	 *
	 *                       ID HDR            ProductVDO
	 *   hdr  SOP R VDMHDR   VDO      VDO      VDO
	 *   0 1  2  3  4        8        12       16
	 * HDMI
	 * 0x8f52 00 00 41a000ff ac32006c 00000000 00000200 18000000
	 *
	 * DP
	 * 0x8f52 00 00 41a000ff ac32006c 00000000 00000300 18000000
	 *
	 * 180W Power Adapter
	 * 0x8f59 00 00 41a800ff ac32c001 00000000 00000e00 01008020
	 *
	 * 60W Power Adapter
	 * 0x8f5b 00 00 41a800ff ac32c001 00000000 01012300 01008020
	 *
	 * 240W Power Adapter
	 * 0x8f5b 00 00 41a800ff ac32c001 00000000 00002200 00000040
	 *
	 * 100W Power Adapter
	 * 0x8f5f 00 00 41a800ff ac32c001 00000000 00002d00 00000040
	 */
	int i;
	uint16_t vid, pid;
	bool trigger_deferred_update = false;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	for (i = 0; i < sizeof(framework_vdm_hdr_match)/sizeof(struct match_vdm_header); i++) {
		if (framework_vdm_hdr_match[i].idx >= len) {
			continue;
		}
		if (data[framework_vdm_hdr_match[i].idx] !=
			framework_vdm_hdr_match[i].val) {
			return;
			}
	}

	for (i = 0; i < sizeof(cypd_altmode_ids)/sizeof(struct framework_dp_ids); i++) {
		vid = cypd_altmode_ids[i].vid;
		pid = cypd_altmode_ids[i].pid;
		if ((vid & 0xFF) == data[8] &&
			((vid>>8) & 0xFF) == data[9] &&
			(pid & 0xFF) == data[18] &&
			((pid>>8) & 0xFF) == data[19]
			) {
			pending_dp_poweroff[port + (controller<<1)] = true;
			trigger_deferred_update = true;
				CPRINTS(" vdm vidpid match");

		}

	}
	if (trigger_deferred_update) {
		hook_call_deferred(&poweroff_dp_deferred_data, 30000 * MSEC);
	}

}

int cypd_handle_alert_msg(int controller, int port, int len)
{
	int rv;
	int port_idx = (controller << 1) + port;
	uint8_t pd_status_reg[4];
	uint32_t alert_event_type;

	if (len > 8) {
		CPRINTS("Alert Massage Too Long");
		return EC_ERROR_INVAL;
	}

	/* Read the extended message packet */
	rv = cypd_read_reg_block(controller,
		CCG_READ_DATA_MEMORY_REG(port, 0), (void *)&(alert_rx[port_idx]), len);

	/* Read the pd port partner status */
	rv = cypd_read_reg_block(controller, CCG_PD_STATUS_REG(port), pd_status_reg, 4);

	CPRINTS("PD Power Button: Data Role = %s",
			pd_status_reg[0] & BIT(6) ? "DFP" : "UFP");
	pd_port_states[port_idx].data_role =
			pd_status_reg[0] & BIT(6) ? PD_ROLE_DFP : PD_ROLE_UFP;

	alert_event_type = ADO_EXTENDED_ALERT_EVENT_TYPE & alert_rx[port_idx].ado;
	CPRINTS("PD Power Button: ADO_EXTENDED_ALERT_EVENT (%X)", alert_event_type);

	/* Extended Alert */
	if ((alert_rx[port_idx].ado & ADO_EXTENDED_ALERT_EVENT) == 0) {
		CPRINTS("PD Power Button: ADO_EXTENDED_ALERT_EVENT bit not set");
		return rv;
	}
	if (pd_port_states[port_idx].data_role != PD_ROLE_DFP) {
		CPRINTS("PD Power Button: Data role not DFP");
		return rv;
	}

	switch (alert_event_type) {
	case ADO_POWER_BUTTON_PRESS:
		/**
		 * follow Framework UI ERS Power Button Behavior
		 * 1. <4 Seconds - Normal power event (Power on, Wake from
		 * suspend, send event to PCH) for OS defined behavior.
		 * 2. >8 seconds, < 12 seconds - Force CPU to G3(chipset_force_shutdown)
		 * 3. >12 Seconds - Forced reset of system(system_reset).
		 *
		 * Set the maximum timer(>12s doing EC reset) to run all PB state machine.
		 */
		CPRINTS("PD Power Button: Simulate press");
		power_button_simulate_press(13000);
		alert_press = 1;
		break;
	case ADO_POWER_BUTTON_RELEASE:
		/**
		 * Re-schedule to a minimal(1) to release the power button when
		 * received the ADO_POWER_BUTTON_RELEASE event.
		 */
		CPRINTS("PD Power Button: Simulate release");
		power_button_simulate_press(1);
		alert_press = 0;
		break;
	default:
		CPRINTS("PD Power Button: ADO_EXTENDED_ALERT_EVENT invalid (%X)", alert_event_type);
		break;
	}
	return rv;
}

void cypd_port_int(int controller, int port)
{
	int i, rv, response_len, response_code;
	uint8_t data2[32] = {0};
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;
	int port_idx = (controller << 1) + port;
	enum tcpci_msg_type sop_type;
	static int snk_transition_flags;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	/* enum pd_msg_type sop_type; */
	rv = i2c_read_offset16_block(i2c_port, addr_flags,
		CCG_PORT_PD_RESPONSE_REG(port), data2, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("PORT_PD_RESPONSE_REG failed");

	print_pd_response_code(controller, port, data2[0], data2[1]);

	response_len = data2[1];
	response_code = data2[0];
	switch (response_code) {
	case CCG_RESPONSE_PORT_DISCONNECT:
		record_ucsi_connector_change_event(controller, port);
		cypd_evaluate_port_profile(controller, port, data2[0]);
		/* release the button if device disconnect and not sent release ado */
		if (alert_press) {
			power_button_simulate_press(1);
			alert_press = 0;
		}
		CPRINTS("PORT_DISCONNECT");
		__fallthrough;
	case CCG_RESPONSE_HARD_RESET_RX:
	case CCG_RESPONSE_TYPE_C_ERROR_RECOVERY:
	case CCG_RESPONSE_HARD_RESET_SENT:
		if (data2[0] == CCG_RESPONSE_HARD_RESET_RX)
			CPRINTS("HARD_RESET_RX");
		if (data2[0] == CCG_RESPONSE_TYPE_C_ERROR_RECOVERY)
			CPRINTS("TYPE_C_ERROR_RECOVERY");
		if (data2[0] == CCG_RESPONSE_HARD_RESET_SENT)
			CPRINTS("CCG_RESPONSE_HARD_RESET_SENT");

		/* Assert prochot until the PMF is updated (Only sink role needs to do this) */
		if (pd_port_states[(controller << 1) + port].power_role == PD_ROLE_SINK &&
		   (prev_charge_port == (controller << 1) + port)) {
#ifdef CONFIG_PD_CCG8_EPR
			update_cpu_power_limit_events(BIT(PD_PROGRESS_DISCONNECTED), 1);
			/* clear the EPR progress when the adapter is removed */
			clear_epr_progress();
#endif

#ifdef CONFIG_PLATFORM_EC_GPU
			set_gpu_gpio(GPIO_FUNC_ACDC, 0);
#endif
		}

		cypd_update_port_state(controller, port);
		/* make sure the type-c state is cleared */
		clear_port_state(controller, port);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			charge_manager_update_dualrole(port_idx, CAP_UNKNOWN);

		break;
	case CCG_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE:
		CPRINTS("CYPD_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE %d", port_idx);
		cypd_update_port_state(controller, port);
		cypd_evaluate_port_profile(controller, port, data2[0]);
#ifdef CONFIG_PD_CCG8_EPR
		/* make sure enter EPR mode only process in S0 state */
		if (chipset_in_state(CHIPSET_STATE_ON))
			cypd_enter_epr_mode(100);

#endif
		break;
	case CCG_RESPONSE_PORT_CONNECT:
		CPRINTS("CYPD_RESPONSE_PORT_CONNECT %d", port_idx);
		record_ucsi_connector_change_event(controller, port);
		cypd_update_port_state(controller, port);
		cypd_evaluate_port_profile(controller, port, data2[0]);
		break;
	case CCG_RESPONSE_SOURCE_CAP_MSG_RX:
		i2c_read_offset16_block(i2c_port, addr_flags,
				CCG_READ_DATA_MEMORY_REG(port, 0), data2, MIN(response_len, 32));

		if (data2[6] & BIT(7)) {
			pd_port_states[port_idx].epr_support = 1;
			CPRINTS("P%d EPR mode capable", port_idx);
		}
#ifdef CONFIG_PLATFORM_EC_BATTERY_CUT_OFF
		if (!battery_is_cut_off() && !battery_cutoff_in_progress())
#endif
			snk_transition_flags = 1;

		break;
#ifdef CONFIG_PD_CCG8_EPR
	case CCG_RESPONSE_EPR_EVENT:
		CPRINTS("CCG_RESPONSE_EPR_EVENT %d", port_idx);
		cypd_update_epr_state(controller, port, response_len);
		cypd_update_port_state(controller, port);
		break;
#endif
	case CCG_RESPONSE_ACCEPT_MSG_RX:
		CPRINTS("CCG_RESPONSE_ACCEPT_MSG_RX %d", port_idx);
		if (snk_transition_flags) {
			if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER))
				charge_manager_force_ceil(port_idx, 500);
			snk_transition_flags = 0;
		}
		break;
	case CCG_RESPONSE_EXT_MSG_SOP_RX:
	case CCG_RESPONSE_EXT_SOP1_RX:
	case CCG_RESPONSE_EXT_SOP2_RX:
		if (data2[0] == CCG_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = TCPCI_MSG_SOP;
		else if (data2[0] == CCG_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = TCPCI_MSG_SOP_PRIME;
		else if (data2[0] == CCG_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = TCPCI_MSG_SOP_PRIME_PRIME;

		cypd_handle_extend_msg(controller, port, response_len, sop_type);
		if (verbose_msg_logging)
			CPRINTS("CYP_RESPONSE_RX_EXT_MSG");
		break;
	case CCG_RESPONSE_OVER_CURRENT:
		CPRINTS("CCG_RESPONSE_OVER_CURRENT %d", port_idx);
		break;
	case CCG_RESPONSE_REVERSE_CURRENT_ERROR:
		CPRINTS("CCG_RESPONSE_REVERSE_CURRENT_ERROR (RCP) %d", port_idx);
		break;
	case CCG_RESPONSE_ALERT_RX:
		cypd_handle_alert_msg(controller, port, response_len);
		CPRINTS("CCG_RESPONSE_ALERT_RX");
		break;
	case CCG_RESPONSE_VDM_RX:
		i2c_read_offset16_block(i2c_port, addr_flags,
			CCG_READ_DATA_MEMORY_REG(port, 0), data2, MIN(response_len, 32));
		cypd_handle_vdm(controller, port, data2, response_len);
#ifdef CONFIG_PLATFORM_EC_FRAMEWORK_LAPTOP_16
		if (controller == PD_CHIP_GPU) {
			gpu_update_pd_vdm(controller, port, data2, response_len);
		}
#endif /*CONFIG_PLATFORM_EC_FRAMEWORK_LAPTOP_16*/
		CPRINTS("CCG_RESPONSE_VDM_RX");

		__fallthrough;
	default:
		if (response_len && verbose_msg_logging) {
			CPRINTF("Port:%d Data:0x", port_idx);
			i2c_read_offset16_block(i2c_port, addr_flags,
				CCG_READ_DATA_MEMORY_REG(port, 0), data2, MIN(response_len, 32));
			for (i = 0; i < response_len; i++)
				CPRINTF("%02x", data2[i]);
			if (response_code >= CCG_RESPONSE_HARD_RESET_SENT)
				CPRINTF(" - RESET or ERROR!!");
			CPRINTF("\n");
		}
		break;
	}
}

void cypd_interrupt(int controller)
{
	int data;
	int rv;
	int clear_mask = 0;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	rv = cypd_get_int(controller, &data);
	if (rv != EC_SUCCESS) {
		return;
	}

	if (data & CCG_DEV_INTR) {
		cypd_device_int(controller);
		clear_mask |= CCG_DEV_INTR;
	}

	if (data & CCG_PORT0_INTR) {
		cypd_port_int(controller, 0);
		clear_mask |= CCG_PORT0_INTR;
	}

	if (data & CCG_PORT1_INTR) {
		cypd_port_int(controller, 1);
		clear_mask |= CCG_PORT1_INTR;
	}

	if (data & CCG_ICLR_INTR)
		clear_mask |= CCG_ICLR_INTR;

	if (clear_mask)
		cypd_clear_int(controller, clear_mask);

	if (data & CCG_UCSI_INTR) {
		ucsi_read_tunnel(controller);
		cypd_clear_int(controller, CCG_UCSI_INTR);
	}
}

void pd0_chip_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(gpio_get_dt_spec(pd_chip_config[PD_CHIP_0].gpio)) == 0)
		task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_0);
}

void pd1_chip_interrupt(enum gpio_signal signal)
{
	if (gpio_pin_get_dt(gpio_get_dt_spec(pd_chip_config[PD_CHIP_1].gpio)) == 0)
		task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_1);
}

static void cypd_ucsi_wait_delay_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_UCSI_PPM_RESET);
}
DECLARE_DEFERRED(cypd_ucsi_wait_delay_deferred);

void cypd_usci_ppm_reset(void)
{
	/* wait PD chip finish UCSI process */
	hook_call_deferred(&cypd_ucsi_wait_delay_deferred_data, 500 * MSEC);
}

/*****************************************************************************/
/* CYPD task */

static int ucsi_tunnel_disabled;

void cypd_interrupt_handler_task(void *p)
{
	int i, j, evt;
	k_timepoint_t ready_battery_update;

	/* Initialize all charge suppliers to 0 */
	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		for (i = 0; i < CHARGE_PORT_COUNT; i++) {
			for (j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
				charge_manager_update_charge(j, i, NULL);
		}
	}

	/* trigger the handle_state to start setup in task */
	task_set_event(TASK_ID_CYPD, (CCG_EVT_STATE_CTRL_0 |
		CCG_EVT_STATE_CTRL_1 | CCG_EVT_STATE_CTRL_GPU));

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		cypd_enable_interrupt(i, true);
		task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0<<i);
	}

	ready_battery_update = sys_timepoint_calc(K_MSEC(READY_BATTERY_UPDATE));

	while (1) {
		evt = task_wait_event(10*MSEC);

		if (firmware_update)
			continue;

		/*
		 * USCI PPM RESET will make PD current setting to default
		 * need setting port current again
		 */
		if (evt & CCG_EVT_UCSI_PPM_RESET)
			cypd_ppm_port_clear();

		if (evt & CCG_EVT_PDO_RESET)
			cypd_port_current_setting();

		if (evt & CCG_EVT_S_CHANGE) {
			for (i = 0; i < PD_CHIP_COUNT; i++) {
				if (cypd_contoller_is_powered(i))
					update_system_power_state(i);
			}
		}

		if (evt & CCG_EVT_UPDATE_PWRSTAT) {
			for (i = 0; i < PD_CHIP_COUNT; i++) {
				if (cypd_contoller_is_powered(i))
					cypd_update_power_status(i);
			}
		}

		if (evt & CCG_EVT_UPDATE_PORTSTATE) {
			for (i = 0; i < PD_PORT_COUNT; i++) {
				int controller = PORT_TO_CONTROLLER(i);
				int port = PORT_TO_CONTROLLER_PORT(i);

				if (cypd_contoller_is_powered(controller))
					cypd_update_port_state(controller, port);
			}
		}

		if (evt & CCG_EVT_INT_CTRL_0)
			cypd_interrupt(0);

		if (evt & CCG_EVT_STATE_CTRL_0) {
			cypd_handle_state(0);
			task_wait_event_mask(TASK_EVENT_TIMER, 10);
		}

		if (evt & CCG_EVT_CHANGE_P0_PDO_LIST) {
			/* update new PDO format to select pdo register */
			for (i = 0; i < PD_CHIP_COUNT; i++) {
				if (cypd_contoller_is_powered(i)) {
					struct pd_port_current_state_t states =
						pd_port_states[PDPORT(i, 0)];
					int profile = states.safety_table[pre_safety_level];

					cypd_changing_source_pdo_list(i, 0, profile);
				}
			}

			task_wait_event_mask(TASK_EVENT_TIMER, 10);
			task_set_event(TASK_ID_CYPD, CCG_EVT_CHANGE_P1_PDO_LIST);
		}

		if (evt & CCG_EVT_CHANGE_P1_PDO_LIST) {
			/* update new PDO format to select pdo register */
			for (i = 0; i < PD_CHIP_COUNT; i++) {
				if (cypd_contoller_is_powered(i) &&
				    pd_chip_config[i].support_max_port == 2) {
					struct pd_port_current_state_t states =
						pd_port_states[PDPORT(i, 1)];
					int profile = states.safety_table[pre_safety_level];

					cypd_changing_source_pdo_list(i, 1, profile);
				}
			}

			task_wait_event_mask(TASK_EVENT_TIMER, 10);
		}

		if (evt & CCG_EVT_DPALT_DISABLE) {
			poweroff_dp_check();
		}

#if defined(CONFIG_PD_CCG8_CUSTOMIZE_BATT_MESSAGE) ||\
	defined(CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE)
		if (evt & CCG_EVT_BATT_UPDATE) {
			bool ready = false;

			if (!sys_timepoint_expired(ready_battery_update))
				ready = false;
			else
				ready = true;

			if (ready) {
				cypd_customize_battery_cap();
				cypd_customize_battery_status();
			}
		}
#endif
		if (evt & CCG_EVT_PDO_C0P0) {
			cypd_set_typec_profile(0, 0);
			cypd_update_port_state(0, 0);

		}

		if (evt & CCG_EVT_PDO_C0P1) {
			cypd_set_typec_profile(0, 1);
			cypd_update_port_state(0, 1);

		}

		if (evt & CCG_EVT_PERFORM_ERROR_RECOVERY)
			for (i = 0; i < PD_CHIP_COUNT; i++) {
				if (cypd_contoller_is_powered(i))
					perform_error_recovery(i);
			}

		/**
		 * below events communicate with the 2nd pd chip, ignore those if the
		 * project only support one pd chip.
		 */
		if (PD_CHIP_COUNT > 1) {
			if (evt & CCG_EVT_STATE_CTRL_1) {
				cypd_handle_state(1);
				task_wait_event_mask(TASK_EVENT_TIMER, 10);
			}

			if (evt & CCG_EVT_INT_CTRL_1)
				cypd_interrupt(1);

			if (evt & CCG_EVT_PDO_C1P0) {
				cypd_set_typec_profile(1, 0);
				cypd_update_port_state(1, 0);
			}

			if (evt & CCG_EVT_PDO_C1P1) {
				cypd_set_typec_profile(1, 1);
				cypd_update_port_state(1, 1);

			}
		}

		/**
		 * below events communicate with the gpu pd chip, ignore those if the
		 * project does not support GPU PD.
		 */
		if (PD_CHIP_COUNT > 2) {
			if (evt & CCG_EVT_STATE_CTRL_GPU) {
				cypd_handle_state(2);
				task_wait_event_mask(TASK_EVENT_TIMER, 10);
			}

			if (evt & CCG_EVT_INT_CTRL_GPU)
				cypd_interrupt(2);

			if (evt & CCG_EVT_PDO_C2P0) {
				cypd_set_typec_profile(2, 0);
				cypd_update_port_state(2, 0);

			}
		}

		if (evt & (CCG_EVT_INT_CTRL_0 | CCG_EVT_INT_CTRL_1 | CCG_EVT_INT_CTRL_GPU |
			CCG_EVT_STATE_CTRL_0 | CCG_EVT_STATE_CTRL_1 | CCG_EVT_STATE_CTRL_GPU)) {
			/*
			 * If we just processed an event or sent some commands
			 * wait a bit for the pd controller to clear any pending
			 * interrupt requests
			 */
			crec_usleep(50);
		}
		if (!ucsi_tunnel_disabled)
			check_ucsi_event_from_host();

		for (i = 0; i < PD_CHIP_COUNT; i++) {
			const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[i].gpio);

			/* Don't no read the interrupt until the PD chips power on */
			if (cypd_contoller_is_powered(i) && (gpio_pin_get_dt(intr) == 0)) {
				task_set_event(TASK_ID_CYPD, 1<<i);
			}
		}
	}
}

/*****************************************************************************/
/* Commmon functions */

enum pd_power_role pd_get_power_role(int port)
{
	return pd_port_states[port].power_role;
}

void pd_request_power_swap(int port)
{
	/* We probably dont need to do this */
	return;
}

void pd_set_new_power_request(int port)
{
	/*
	 * if the old port have change the request
	 * need to reopen the vbus port again,
	 * eg: after PD send again CCG_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE
	 * will set a new power request, the VBUS port need to open again.
	 */
	if (get_active_charge_pd_port() == port && pd_is_connected(port))
		board_set_active_charge_port(port);
	return;
}

int pd_is_connected(int port)
{
	return pd_port_states[port].c_state != CCG_STATUS_NOTHING;
}

int pd_get_active_current(int port)
{
	return pd_port_states[port].current;
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}



uint8_t *get_pd_version(int controller)
{
	/**
	 * If the PD chip does not ready or not exit, return the 0x00 version.
	 * E.g. dGPU board is not connected, PD chip initial fail...
	 */
	if (controller >= PD_CHIP_COUNT ||
		pd_chip_config[controller].state != CCG_STATE_READY) {
		static uint8_t version[8] = {0};
		return version;
	}

	return pd_chip_config[controller].version;
}

int active_charge_pd_chip(void)
{
	if (prev_charge_port == -1)
		return 0xff;

	return PORT_TO_CONTROLLER(prev_charge_port);
}

int get_active_charge_pd_port(void)
{
	/**
	 * charge_manager_get_active_charge_port() return the charger port too late,
	 * we need to get the active port status immediately.
	 */

	return prev_charge_port;
}

void update_active_charge_pd_port(int update_charger_port)
{
	CPRINTS("%s port %d, prev:%d", __func__, update_charger_port, prev_charge_port);

	prev_charge_port = update_charger_port;
}

void set_pd_fw_update(bool is_update)
{
	firmware_update = is_update;
}

bool cypd_fw_update_in_progress(void)
{
	return firmware_update;
}

void cypd_reinitialize(void)
{
	int i;

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		pd_chip_config[i].state = CCG_STATE_POWER_ON;
		/* Run state handler to set up controller */
		task_set_event(TASK_ID_CYPD, 4<<i);
	}
}

struct pd_port_current_state_t *get_pd_port_states_array(void)
{
	return pd_port_states;
}

int get_pd_alt_mode_status(int port)
{
	int alt_mode_status;

	cypd_read_reg8(PORT_TO_CONTROLLER(port),
		CCG_DP_ALT_MODE_CONFIG_REG(PORT_TO_CONTROLLER_PORT(port)),
		&alt_mode_status);

	return alt_mode_status;
}

static int cypd_controller_port_to_charge_port(int controller, int port)
{
	int charge_port = 0;
	int pd_chip;

	for (pd_chip = 0; pd_chip < controller; pd_chip++)
		charge_port += pd_chip_config[pd_chip].support_max_port;

	charge_port += port;

	return charge_port;
}

void perform_error_recovery(int controller)
{
	int port;
	uint8_t data[2] = {0x00, CCG_PD_USER_CMD_TYPEC_ERR_RECOVERY};

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller) || !IS_ENABLED(CONFIG_PD_ERROR_RECOVERY))
		return;

	/**
	 * There are two type-c ports for each PD chip.
	 * Hard reset all ports that are not supplying power in dead battery mode or
	 * battery percentage less than 1%.
	 */
	for (port = 0; port < pd_chip_config[controller].support_max_port; port++) {
		int pd_port;

		pd_port = board_perform_error_recovery_port(port);

#ifdef CONFIG_PLATFORM_EC_BATTERY
		if (cypd_controller_port_to_charge_port(controller, pd_port) ==
			get_active_charge_pd_port() &&
		    (battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED ||
		    (charge_get_percent() < 1)))
			continue;
#endif
		data[0] = pd_port;
		cypd_write_reg_block(controller, CCG_DPM_CMD_REG, data, 2);
	}
}

int cypd_get_active_pd_chip_count(void)
{
	int active_pd_chip_count = 0;

	for (int i = 0; i < PD_CHIP_COUNT; i++) {
		if (!cypd_contoller_is_powered(i))
			continue;

		active_pd_chip_count++;
	}

	return active_pd_chip_count;
}

static void cypd_reset_pd_chip(int chip)
{
	cypd_write_reg8(chip, CCG_PDPORT_ENABLE_REG, 0);

	/*can take up to 650ms to discharge port for disable*/
	cypd_wait_for_ack(chip, 650);

	cypd_clear_int(chip,
		CCG_DEV_INTR + CCG_PORT0_INTR + CCG_PORT1_INTR + CCG_UCSI_INTR);

	crec_msleep(1000);

	/*
	 * see if we can talk to the PD chip yet - issue a reset command
	 * Note that we cannot issue a full reset command if the PD controller
	 * has a device attached - as it will return with an invalid command
	 * due to needing to disable all ports first.
	 */
	if (cypd_reset(chip) == EC_SUCCESS)
		CPRINTS("Full reset PD controller %d", chip);

}

void board_reset_pd_mcu(void)
{

#ifndef CONFIG_PLATFORM_EC_PD_RESET_BEFORE_EC_REBOOT
	return;
#endif

	for (int controller = 0; controller < PD_CHIP_COUNT; controller++) {

		/**
		 * When the EC recevies the reboot command from the host, EC will
		 * auto power on the system via hard reset flag. However, if resetting
		 * the PD chip without battery, the reset flag will be power-on.
		 * Therefore, we should update the ac power on flag to auto power on
		 * the system.
		 */
#ifndef CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC
		flash_storage_update(FLASH_FLAGS_ACPOWERON, get_standalone_mode());
		flash_storage_commit();
#endif

		cypd_reset_pd_chip(controller);
	}

	crec_msleep(500);
}

/*****************************************************************************/
/* Host command */

/*****************************************************************************/
/* EC console command */

static int cmd_cypd_get_status(int argc, const char **argv)
{
	int i, p, data;
	uint8_t data16[16];
	char *e;

	static const char * const mode[] = {"Boot", "FW1", "FW2", "Invald"};
	static const char * const current_level[] = {"DefaultA", "1.5A", "3A", "InvA"};
	static const char * const port_status[] = {
		"Nothing", "Sink", "Source", "Debug", "Audio", "Powered Acc",
		"Unsupported", "Invalid"
	};
	static const char * const state[] = {
		"ERR", "NO_POWER", "WAIT_STABLE", "POWER_ON", "APP_SETUP", "READY", "BOOTLOADER"
	};
	const struct gpio_dt_spec *intr;

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		intr = gpio_get_dt_spec(pd_chip_config[i].gpio);
		CPRINTS("PD%d INT value: %d", i, gpio_pin_get_dt(intr));
	}

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		if (i < PD_CHIP_COUNT) {
			CPRINTS("State: %s", state[pd_chip_config[i].state]);
			cypd_read_reg16(i, CCG_SILICON_ID, &data);
			CPRINTS("CYPD_SILICON_ID: 0x%04x", data);
			cypd_get_version(i);
			cypd_read_reg8(i, CCG_DEVICE_MODE, &data);
			CPRINTS("CYPD_DEVICE_MODE: 0x%02x %s", data, mode[data & 0x03]);
			cypd_read_reg_block(i, CCG_HPI_VERSION, data16, 4);
			CPRINTS("HPI_VERSION: 0x%02x%02x%02x%02x",
						data16[3], data16[2], data16[1], data16[0]);
			cypd_read_reg8(i, CCG_INTR_REG, &data);
			CPRINTS("CYPD_INTR_REG: 0x%02x %s %s %s %s",
						data,
						data & CCG_DEV_INTR ? "DEV" : "",
						data & CCG_PORT0_INTR ? "PORT0" : "",
						data & CCG_PORT1_INTR ? "PORT1" : "",
						data & CCG_UCSI_INTR ? "UCSI" : "");
			cypd_read_reg16(i, CCG_RESPONSE_REG, &data);
			CPRINTS("CYPD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg16(i, CCG_PORT_PD_RESPONSE_REG(0), &data);
			CPRINTS("CYPD_PORT0_PD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg16(i, CCG_PORT_PD_RESPONSE_REG(1), &data);
			CPRINTS("CYPD_PORT1_PD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg8(i, CCG_BOOT_MODE_REASON, &data);
			CPRINTS("CYPD_BOOT_MODE_REASON: 0x%02x", data);
			cypd_read_reg8(i, CCG_PDPORT_ENABLE_REG, &data);
			CPRINTS("CYPD_PDPORT_ENABLE_REG: 0x%04x", data);
			cypd_read_reg8(i, CCG_POWER_STAT, &data);
			CPRINTS("CYPD_POWER_STAT: 0x%02x", data);
			cypd_read_reg8(i, CCG_ICL_STS_REG, &data);
			CPRINTS("CCG_ICL_STS_REG: 0x%04x", data);
			cypd_read_reg8(i, CCG_SYS_PWR_STATE, &data);
			CPRINTS("CYPD_SYS_PWR_STATE: 0x%02x", data);
			for (p = 0; p < pd_chip_config[i].support_max_port; p++) {
				CPRINTS("=====Port %d======", p);
				cypd_read_reg_block(i, CCG_PD_STATUS_REG(p), data16, 4);
				CPRINTS("PD_STATUS %s DataRole:%s PowerRole:%s Vconn:%s Partner:%s EPR:%s %sCable:%s",
						data16[1] & BIT(2) ? "Contract" : "NoContract",
						data16[0] & BIT(6) ? "DFP" : "UFP",
						data16[1] & BIT(0) ? "Source" : "Sink",
						data16[1] & BIT(5) ? "En" : "Dis",
						data16[2] & BIT(3) ? "Un-chunked" : "Chunked",
						data16[2] & BIT(7) ? "EPR" : "Non EPR",
						data16[1] & BIT(3) ? "EMCA " : "",
						data16[2] & BIT(6) ? "Active" : "Passive");
				cypd_read_reg8(i, CCG_TYPE_C_STATUS_REG(p), &data);
				CPRINTS("   TYPE_C_STATUS : %s %s %s %s %s",
							data & 0x1 ? "Connected" : "Not Connected",
							data & 0x2 ? "CC2" : "CC1",
							port_status[(data >> 2) & 0x7],
							data & 0x20 ? "Ra" : "NoRa",
							current_level[(data >> 6) & 0x03]);
				cypd_read_reg8(i, CCG_PORT_VBUS_FET_CONTROL(p), &data);
				CPRINTS("        VBUS_FET : %s %s",
						data & 0x1 ? "EC" : "Auto",
						data & 0x2 ? "On" : "Off");
				cypd_read_reg_block(i, CCG_CURRENT_RDO_REG(p), data16, 4);
				CPRINTS("             RDO : Current:%dmA MaxCurrent%dmA 0x%08x",
					(((data16[1] >> 2) + (data16[2] << 6)) & 0x3FF) * 10,
					((data16[0] + (data16[1] << 8)) & 0x3FF) * 10,
					*(uint32_t *)data16);

				cypd_read_reg_block(i, CCG_CURRENT_PDO_REG(p), data16, 4);
				CPRINTS("             PDO : MaxCurrent:%dmA Voltage%dmA 0x%08x",
						((data16[0] + (data16[1]<<8)) & 0x3FF)*10,
						(((data16[1]>>2) + (data16[2]<<6)) & 0x3FF)*50,
						*(uint32_t *)data16);
				cypd_read_reg8(i, CCG_TYPE_C_VOLTAGE_REG(p), &data);
				CPRINTS("  TYPE_C_VOLTAGE : %dmV", data*100);
				cypd_read_reg8(i, CCG_PORT_CURRENT_REG(p), &data);
				CPRINTS("  TYPE_C_CURRENT : %dmA", data*50);
				cypd_read_reg_block(i, CCG_PORT_INTR_STATUS_REG(p), data16, 4);
				cypd_print_buff("      INTR_STATUS:", data16, 4);
				cypd_read_reg16(i, SELECT_SINK_PDO_EPR_MASK(p), &data);
				CPRINTS(" SINK PDO EPR MASK: 0x%02x", data);
				/* Flush console to avoid truncating output */
				cflush();
			}
			CPRINTS("=====UCSI======");
			cypd_read_reg16(i, CCG_VERSION_REG, &data);
			CPRINTS(" Version: 0x%04x", data);
			cypd_read_reg_block(i, CCG_CCI_REG, data16, 4);
			cypd_print_buff("     CCI:", data16, 4);
			cypd_read_reg_block(i, CCG_CONTROL_REG, data16, 8);
			cypd_print_buff(" Control:", data16, 8);
			cypd_read_reg_block(i, CCG_MESSAGE_IN_REG, data16, 16);
			cypd_print_buff(" Msg  In:", data16, 16);
			cypd_read_reg_block(i, ucsi_message_out_offset(), data16, 16);
			cypd_print_buff(" Msg Out:", data16, 16);
		}

	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cypdstatus, cmd_cypd_get_status, "[number]",
			"Get Cypress PD controller status");

static int cmd_cypd_control(int argc, const char **argv)
{
	int i, enable;
	char *e;

	if (argc >= 3) {
		i = strtoi(argv[2], &e, 0);
		if (*e || i >= PD_CHIP_COUNT)
			return EC_ERROR_PARAM2;

		if (!strncmp(argv[1], "en", 2) || !strncmp(argv[1], "dis", 3)) {
			if (!parse_bool(argv[1], &enable))
				return EC_ERROR_PARAM1;
			if (enable)
				cypd_enable_interrupt(i, true);
			else
				cypd_enable_interrupt(i, false);
		} else if (!strncmp(argv[1], "reset", 5)) {
			cypd_reset_pd_chip(i);
		} else if (!strncmp(argv[1], "clearint", 8)) {
			cypd_clear_int(i, CCG_DEV_INTR +
					  CCG_PORT0_INTR +
					  CCG_PORT1_INTR +
					  CCG_UCSI_INTR);
		} else if (!strncmp(argv[1], "verbose", 7)) {
			verbose_msg_logging = (i != 0);
			CPRINTS("verbose=%d", verbose_msg_logging);
		} else if (!strncmp(argv[1], "ucsitun", 7)) {
			ucsi_tunnel_disabled = !i;
			CPRINTS("ucsi tun=%d", i);
		} else if (!strncmp(argv[1], "ucsi", 4)) {
			ucsi_set_debug(i != 0);
			CPRINTS("ucsi verbose=%d", i);
		} else if (!strncmp(argv[1], "powerstate", 10)) {
			int pwrstate;

			if (argc < 4)
				return EC_ERROR_PARAM3;
			pwrstate = strtoul(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
			cypd_set_power_state(pwrstate, 2);
		} else if (!strncmp(argv[1], "write16", 3)) {
			int r;
			int regval;
			if (argc < 5) {
				return EC_ERROR_PARAM4;
			}
			r = strtoul(argv[3], &e, 0);
			regval = strtoul(argv[4], &e, 0);
			cypd_write_reg16(i, r,  regval);
		} else if (!strncmp(argv[1], "write8", 3)) {
			int r;
			int regval;

			if (argc < 5)
				return EC_ERROR_PARAM4;
			r = strtoul(argv[3], &e, 0);
			regval = strtoul(argv[4], &e, 0);
			cypd_write_reg8(i, r,  regval);
		} else if (!strncmp(argv[1], "read16", 2)) {
			int r;
			int regval;

			if (argc < 4)
				return EC_ERROR_PARAM3;
			r = strtoul(argv[3], &e, 0);
			cypd_read_reg16(i, r,  &regval);
			CPRINTS("data=%d", regval);
		} else if (!strncmp(argv[1], "read8", 2)) {
			int r;
			int regval;

			if (argc < 4)
				return EC_ERROR_PARAM3;
			r = strtoul(argv[3], &e, 0);
			cypd_read_reg8(i, r,  &regval);
			CPRINTS("data=%d", regval);
		} else {
			return EC_ERROR_PARAM1;
		}
	} else {
		return EC_ERROR_PARAM_COUNT;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cypdctl, cmd_cypd_control,
			"[enable/disable/reset/clearint/verbose/ucsi] [controller]",
			"Set if handling is active for controller");


static int cmd_pdwrite(int argc, const char **argv)
{
	int controller, addr, data, rv;
	char *e;

	controller = strtoi(argv[1], &e, 0);
	addr = strtoi(argv[2], &e, 0);
	data = strtoi(argv[3], &e, 0);

	if (controller > 1)
		return EC_ERROR_PARAM1;

	CPRINTS("controller:%d ,addr:%x ,data:%d", controller, addr, data);

	rv = cypd_write_reg8_wait_ack(controller, addr, data);
	if (rv != EC_SUCCESS)
		CPRINTS("Write data fail");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pdwrite, cmd_pdwrite,
			"[controller] [addr] [data]",
			"write data to PD");

static int cmd_pdread(int argc, const char **argv)
{
	int controller, addr, data, rv;
	char *e;

	controller = strtoi(argv[1], &e, 0);
	addr = strtoi(argv[2], &e, 0);

	if (controller > 1)
		return EC_ERROR_PARAM1;

	rv = cypd_read_reg16(controller, addr, &data);
	if (rv != EC_SUCCESS)
		CPRINTS("Write data fail");

	CPRINTS("controller:%d ,addr:%x ,data:%d", controller, addr, data);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pdread, cmd_pdread,
			"[controller] [addr]",
			"read data from PD");
