/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"
#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_tc_sm.h"
#include "util.h"
#include "zephyr_console_shim.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/*
 * Unimplemented functions:
 * 1. Control port current 3A/1.5A for GRL test.
 * 2. Control port VBUS enable/disable.
 * 3. Update system power state to PD chip. (Avoid PD chip does the error recovery)
 * 4. Control PD chip compliance mode
 * 5. Flash PD flow
 * 6. Extended message handler
 * 7. UCSI handler
 */

static struct pd_chip_config_t pd_chip_config[] = {
	[PD_CHIP_0] = {
		.i2c_port = I2C_PORT_PD_MCU0,
		.addr_flags = CCG_I2C_CHIP0 | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
		.state = CCG_STATE_POWER_ON,
		.gpio = GPIO_EC_PD_INTA_L,
	},
	[PD_CHIP_1] = {
		.i2c_port = I2C_PORT_PD_MCU1,
		.addr_flags = CCG_I2C_CHIP1 | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
		.state = CCG_STATE_POWER_ON,
		.gpio = GPIO_EC_PD_INTB_L,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pd_chip_config) == PD_CHIP_COUNT);

static struct pd_port_current_state_t pd_port_states[] = {
	[PD_PORT_0] = {

	},
	[PD_PORT_1] = {

	},
	[PD_PORT_2] = {

	},
	[PD_PORT_3] = {

	}
};

static int prev_charge_port = -1;
static bool verbose_msg_logging;
static bool firmware_update;

/*****************************************************************************/
/* Internal functions */

static int cypd_write_reg_block(int controller, int reg, void *data, int len)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_write_offset16_block(i2c_port, addr_flags, reg, data, len);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

static int cypd_write_reg16(int controller, int reg, int data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_write_offset16(i2c_port, addr_flags, reg, data, 2);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

static int cypd_write_reg8(int controller, int reg, int data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_write_offset16(i2c_port, addr_flags, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

static int cypd_read_reg_block(int controller, int reg, void *data, int len)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16_block(i2c_port, addr_flags, reg, data, len);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

static int cypd_read_reg16(int controller, int reg, int *data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16(i2c_port, addr_flags, reg, data, 2);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

static int cypd_read_reg8(int controller, int reg, int *data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16(i2c_port, addr_flags, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
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

static int cypd_get_int(int controller, int *intreg)
{
	int rv;

	rv = cypd_read_reg8(controller, CCG_INTR_REG, intreg);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, rv=0x%02x", __func__, controller, rv);
	return rv;
}

static int cypd_clear_int(int controller, int mask)
{
	int rv;

	rv = cypd_write_reg8(controller, CCG_INTR_REG, mask);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, mask=0x%02x", __func__, controller, mask);
	return rv;
}

static int cypd_wait_for_ack(int controller, int timeout_us)
{
	int timeout;
	const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[controller].gpio);

	timeout_us = timeout_us / 10;
	/* wait for interrupt ack to be asserted */
	for (timeout = 0; timeout < timeout_us; timeout++) {
		if (gpio_pin_get_dt(intr) == 0)
			break;
		usleep(10);
	}
	/* make sure response is ok */
	if (gpio_pin_get_dt(intr) != 0) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

static int cypd_write_reg8_wait_ack(int controller, int reg, int data)
{
	int rv = EC_SUCCESS;
	int intr_status;

	rv = cypd_write_reg8(controller, reg, data);
	if (rv != EC_SUCCESS)
		CPRINTS("Write Reg8 0x%x fail!", reg);

	if (cypd_wait_for_ack(controller, 100*MSEC) != EC_SUCCESS) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	rv = cypd_get_int(controller, &intr_status);
	if (intr_status & CCG_DEV_INTR)
		cypd_clear_int(controller, CCG_DEV_INTR);
	usleep(50);
	return rv;
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

static void update_power_state_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_UPDATE_PWRSTAT);
}
DECLARE_DEFERRED(update_power_state_deferred);

static void cypd_enable_interrupt(int controller, int enable_ndisable)
{
	if (controller) {
			if (enable_ndisable)
				gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip1_interrupt));
			else
				gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip1_interrupt));
	}else {
			if (enable_ndisable)
				gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip0_interrupt));
			else
				gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_pd_chip0_interrupt));
	}
}

static void cypd_print_version(int controller, const char *vtype, uint8_t *data)
{
	/*
	 * Base version: Cypress release version
	 * Application version: FAE release version
	 */
	CPRINTS("Controller %d  %s version B:%d.%d.%d.%d, AP:%d.%d.%d.",
		controller, vtype,
		(data[3]>>4) & 0xF, (data[3]) & 0xF, data[2], data[0] + (data[1]<<8),
		(data[7]>>4) & 0xF, (data[7]) & 0xF, data[6]);
}

static void cypd_get_version(int controller)
{
	int rv;
	int i;
	uint8_t data[24];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16_block(i2c_port, addr_flags, CCG_READ_ALL_VERSION_REG, data, 24);
	if (rv != EC_SUCCESS)
		CPRINTS("READ_ALL_VERSION_REG failed");

	cypd_print_version(controller, "App1", data+8);
	cypd_print_version(controller, "App2", data+16);

	/* store the FW2 version into pd_chip_info struct */
	for (i = 0; i < 8; i++)
		pd_chip_config[controller].version[i] = data[16+i];
}

static void cypd_update_port_state(int controller, int port)
{
	int rv;
	uint8_t pd_status_reg[4];
	uint32_t pdo_reg;
	uint8_t rdo_reg[4];

	int typec_status_reg;
	int pd_current = 0;
	int pd_voltage = 0;
	int rdo_max_current = 0;
	int type_c_current = 0;
	int port_idx = (controller << 1) + port;

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

	if (pd_voltage > 20000) {
		/* (charge_ma * charge_mv / 20000 ) * 0.9 */
		pd_current = pd_current * pd_voltage / 20000;
	} 

	cypd_read_reg_block(controller, CCG_CURRENT_RDO_REG(port), rdo_reg, 4);
	rdo_max_current = (((rdo_reg[1]>>2) + (rdo_reg[2]<<6)) & 0x3FF)*10;

	/*
	 * The port can have several states active:
	 * 1. Type C active (with no PD contract) CC resistor negociation only
	 * 2. Type C active with PD contract
	 * 3. Not active
	 * Each of 1 and 2 can be either source or sink
	 */

	if (pd_port_states[port_idx].c_state == CCG_STATUS_SOURCE) {
		typec_set_input_current_limit(port_idx, type_c_current, TYPE_C_VOLTAGE);
		charge_manager_set_ceil(port_idx, CEIL_REQUESTOR_PD,
							type_c_current);
	} else {
		typec_set_input_current_limit(port_idx, 0, 0);
		charge_manager_set_ceil(port,
			CEIL_REQUESTOR_PD,
			CHARGE_CEIL_NONE);
	}
	if (pd_port_states[port_idx].c_state == CCG_STATUS_SINK) {
		pd_port_states[port_idx].current = type_c_current;
		pd_port_states[port_idx].voltage = TYPE_C_VOLTAGE;
	}

	if (pd_port_states[port_idx].pd_state) {
		if (pd_port_states[port_idx].power_role == PD_ROLE_SINK) {
			pd_set_input_current_limit(port_idx, pd_current, pd_voltage);
			charge_manager_set_ceil(port_idx, CEIL_REQUESTOR_PD,
								pd_current);
			pd_port_states[port_idx].current = pd_current;
			pd_port_states[port_idx].voltage = pd_voltage;
		} else {
			pd_set_input_current_limit(port_idx, 0, 0);
			/*Source*/
			pd_port_states[port_idx].current = rdo_max_current;
			pd_port_states[port_idx].voltage = TYPE_C_VOLTAGE;

		}
	} else {
		pd_set_input_current_limit(port_idx, 0, 0);
	}

	if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER)) {
		charge_manager_update_dualrole(port_idx, CAP_DEDICATED);
	}
}

static void cypd_update_epr_state(int controller, int port, int response_len)
{
	int rv;
	uint8_t data[16];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16_block(i2c_port, addr_flags,
		CCG_READ_DATA_MEMORY_REG(port, 0), data, MIN(response_len, 16));

	if (rv != EC_SUCCESS)
		CPRINTS("CCG_READ_DATA_MEMORY_REG failed");

	if ((data[0] & EPR_EVENT_POWER_ROLE_MASK) == EPR_EVENT_POWER_ROLE_SINK) {
		switch (data[0] & EPR_EVENT_TYPE_MASK) {
		case EPR_MODE_ENTERED:
			CPRINTS("Entered EPR");
			break;
		case EPR_MODE_EXITED:
			CPRINTS("Exited EPR");
			break;
		case EPR_MODE_ENTER_FAILED:
		default:
		/* see epr_event_failure_type*/
			CPRINTS("EPR failed %d", data[1]);
		}
	}
}

static int cypd_update_power_status(int controller)
{
	int i;
	int rv = EC_SUCCESS;
	int power_stat = 0;

	if (battery_is_present() == BP_YES)
		power_stat |= BIT(3);
	if (extpower_is_present())
		power_stat |= BIT(1) + BIT(2);

	CPRINTS("C%d, %s power_stat 0x%x", controller, __func__, power_stat);
	if (controller < PD_CHIP_COUNT)
		rv = cypd_write_reg8_wait_ack(controller, CCG_POWER_STAT, power_stat);
	else {
		for (i = 0; i < PD_CHIP_COUNT; i++) {
			rv = cypd_write_reg8_wait_ack(i, CCG_POWER_STAT, power_stat);
			if (rv != EC_SUCCESS)
				break;
		}
	}
	return rv;
}

#define CYPD_SETUP_CMDS_LEN 5
static int cypd_setup(int controller)
{
	/*
	 * 1. CCG notifies EC with "RESET Complete event after Reset/Power up/JUMP_TO_BOOT
	 * 2. EC Reads DEVICE_MODE register does not in Boot Mode
	 * 3. CCG will enters 100ms timeout window and waits for "EC Init Complete" command
	 * 4. EC sets Source and Sink PDO mask if required
	 * 5. EC sets Event mask if required
	 * 6. EC sends EC Init Complete Command
	 */

	int rv, data, i;
	const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[controller].gpio);
	struct {
		int reg;
		int value;
		int length;
		int status_reg;
	} const cypd_setup_cmds[] = {
		/* Set the port PDO 1.5A */
		{ CCG_PD_CONTROL_REG(0), CCG_PD_CMD_SET_TYPEC_1_5A, CCG_PORT0_INTR},
		{ CCG_PD_CONTROL_REG(1), CCG_PD_CMD_SET_TYPEC_1_5A, CCG_PORT1_INTR},
		/* Set the port event mask */
		{ CCG_EVENT_MASK_REG(0), 0x27ffff, 4, CCG_PORT0_INTR},
		{ CCG_EVENT_MASK_REG(1), 0x27ffff, 4, CCG_PORT1_INTR },
		/* EC INIT Complete */
		{ CCG_PD_CONTROL_REG(0), CCG_PD_CMD_EC_INIT_COMPLETE, CCG_PORT0_INTR },
	};
	BUILD_ASSERT(ARRAY_SIZE(cypd_setup_cmds) == CYPD_SETUP_CMDS_LEN);

	/* Make sure the interrupt is not asserted before we start */
	if (gpio_pin_get_dt(intr) == 0) {
		rv = cypd_get_int(controller, &data);
		CPRINTS("%s int already pending 0x%04x", __func__, data);
		cypd_clear_int(controller,
			CCG_DEV_INTR + CCG_PORT0_INTR + CCG_PORT1_INTR + CCG_UCSI_INTR);
	}
	for (i = 0; i < CYPD_SETUP_CMDS_LEN; i++) {
		rv = cypd_write_reg_block(controller, cypd_setup_cmds[i].reg,
		(void *)&cypd_setup_cmds[i].value, cypd_setup_cmds[i].length);
		if (rv != EC_SUCCESS) {
			CPRINTS("%s command: 0x%04x failed", __func__, cypd_setup_cmds[i].reg);
			return EC_ERROR_INVAL;
		}
		/* wait for interrupt ack to be asserted */
		if (cypd_wait_for_ack(controller, 5000) != EC_SUCCESS) {
			CPRINTS("%s timeout on interrupt", __func__);
			return EC_ERROR_INVAL;
		}

		/* clear cmd ack */
		cypd_clear_int(controller, cypd_setup_cmds[i].status_reg);
	}
	return EC_SUCCESS;
}

static void cypd_handle_state(int controller)
{
	int data;
	int delay = 0;

	switch (pd_chip_config[controller].state) {
	case CCG_STATE_POWER_ON:
		/* poll to see if the controller has booted yet */
		if (cypd_read_reg8(controller, CCG_DEVICE_MODE, &data) == EC_SUCCESS) {
			if ((data & 0x03) == 0x00) {
				CPRINTS("CYPD %d is in bootloader 0x%04x", controller, data);
				delay = 25*MSEC;
				if (cypd_read_reg16(controller, CCG_BOOT_MODE_REASON, &data)
						== EC_SUCCESS) {
					CPRINTS("CYPD bootloader reason 0x%02x", data);
				}

			} else
				pd_chip_config[controller].state = CCG_STATE_APP_SETUP;
		}
		/*try again in a while*/
		if (delay) {
			if (controller == 0)
				hook_call_deferred(&pd0_update_state_deferred_data, delay);
			else
				hook_call_deferred(&pd1_update_state_deferred_data, delay);
		} else
			task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0 << controller);
		break;

	case CCG_STATE_APP_SETUP:
			gpio_disable_interrupt(pd_chip_config[controller].gpio);
			cypd_get_version(controller);
			cypd_update_power_status(controller);

			/*
			 * Avoid the error recovery happened, disable to change the CCG power state
			 * cypd_set_power_state(CCG_POWERSTATE_S5, controller);
			 */
			cypd_setup(controller);

			/* After initial complete, update the type-c port state */
			cypd_update_port_state(controller, 0);
			cypd_update_port_state(controller, 1);

			/*
			 * TODO: enable UCSI function
			 * cyp5525_ucsi_startup(controller);
			 */

			gpio_enable_interrupt(pd_chip_config[controller].gpio);

			/*
			 * Avoid the error recovery happened, disable to change the CCG power state
			 * update_system_power_state(controller);
			 */

			CPRINTS("CYPD %d Ready!", controller);
			pd_chip_config[controller].state = CCG_STATE_READY;
		break;
	default:
		CPRINTS("PD handle_state but in 0x%02x state!", pd_chip_config[controller].state);
		break;
	}

}

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
/* Interrupt handler */

int cypd_device_int(int controller)
{
	int data;

	if (cypd_read_reg16(controller, CCG_RESPONSE_REG, &data) == EC_SUCCESS) {

		print_pd_response_code(controller, -1, data & 0xff, data>>8);

		switch (data & 0xFF) {
		case CCG_RESPONSE_RESET_COMPLETE:
			CPRINTS("PD%d Reset Complete", controller);

			pd_chip_config[controller].state = CCG_STATE_POWER_ON;
			/* Run state handler to set up controller */
			task_set_event(TASK_ID_CYPD, 4 << controller);
			break;
		default:
			CPRINTS("INTR_REG CTRL:%d TODO Device 0x%x", controller, data & 0xFF);
		}
	} else
		return EC_ERROR_INVAL;


	return EC_SUCCESS;
}

void cypd_port_int(int controller, int port)
{
	int i, rv, response_len;
	uint8_t data2[32];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;
	int port_idx = (controller << 1) + port;
	/* enum pd_msg_type sop_type; */
	rv = i2c_read_offset16_block(i2c_port, addr_flags,
		CCG_PORT_PD_RESPONSE_REG(port), data2, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("PORT_PD_RESPONSE_REG failed");

	print_pd_response_code(controller, port, data2[0], data2[1]);

	response_len = data2[1];
	switch (data2[0]) {
	case CCG_RESPONSE_PORT_DISCONNECT:
		CPRINTS("CYPD_RESPONSE_PORT_DISCONNECT");
		pd_port_states[port_idx].current = 0;
		pd_port_states[port_idx].voltage = 0;
		pd_set_input_current_limit(port_idx, 0, 0);
		cypd_update_port_state(controller, port);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			charge_manager_update_dualrole(port_idx, CAP_UNKNOWN);
		break;
	case CCG_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE:
		CPRINTS("CYPD_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE %d", port_idx);
		cypd_update_port_state(controller, port);
		break;
	case CCG_RESPONSE_PORT_CONNECT:
		CPRINTS("CYPD_RESPONSE_PORT_CONNECT %d", port_idx);
		cypd_update_port_state(controller, port);
		break;
	case CCG_RESPONSE_EPR_EVENT:
		CPRINTS("CCG_RESPONSE_EPR_EVENT %d", port_idx);
		cypd_update_epr_state(controller, port, response_len);
		cypd_update_port_state(controller, port);
		break;
	default:
		if (response_len && verbose_msg_logging) {
			CPRINTF("Port:%d Data:0x", port_idx);
			i2c_read_offset16_block(i2c_port, addr_flags,
				CCG_READ_DATA_MEMORY_REG(port, 0), data2, MIN(response_len, 32));
			for (i = 0; i < response_len; i++)
				CPRINTF("%02x", data2[i]);
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

	if (data & CCG_UCSI_INTR) {
		/*
		 * TODO: implement ucsi_read_tunnel(controller);
		 */
		cypd_clear_int(controller, CCG_UCSI_INTR);
	}

	cypd_clear_int(controller, clear_mask);
}

void pd0_chip_interrupt_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_0);

}
DECLARE_DEFERRED(pd0_chip_interrupt_deferred);

void pd1_chip_interrupt_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CCG_EVT_INT_CTRL_1);

}
DECLARE_DEFERRED(pd1_chip_interrupt_deferred);

void pd0_chip_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&pd0_chip_interrupt_deferred_data, 0);
}

void pd1_chip_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&pd1_chip_interrupt_deferred_data, 0);
}

/*****************************************************************************/
/* CYPD task */

void cypd_interrupt_handler_task(void *p)
{
	int i, j, evt;

	/* Initialize all charge suppliers to 0 */
	for (i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	/* trigger the handle_state to start setup in task */
	task_set_event(TASK_ID_CYPD, (CCG_EVT_STATE_CTRL_0 | CCG_EVT_STATE_CTRL_1));

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		cypd_enable_interrupt(i, true);
		task_set_event(TASK_ID_CYPD, CCG_EVT_STATE_CTRL_0<<i);
	}

	while (1) {
		evt = task_wait_event(10*MSEC);

		if (firmware_update)
			continue;

		if (evt & CCG_EVT_INT_CTRL_0)
			cypd_interrupt(0);

		if (evt & CCG_EVT_INT_CTRL_1)
			cypd_interrupt(1);

		if (evt & CCG_EVT_STATE_CTRL_0) {
			cypd_handle_state(0);
			task_wait_event_mask(TASK_EVENT_TIMER,10);
		}

		if (evt & CCG_EVT_STATE_CTRL_1) {
			cypd_handle_state(1);
			task_wait_event_mask(TASK_EVENT_TIMER,10);
		}

		if (evt & (CCG_EVT_INT_CTRL_0 | CCG_EVT_INT_CTRL_1 |
					CCG_EVT_STATE_CTRL_0 | CCG_EVT_STATE_CTRL_1)) {
			/*
			 * If we just processed an event or sent some commands
			 * wait a bit for the pd controller to clear any pending
			 * interrupt requests
			 */
			usleep(50);
		}

		/* check_ucsi_event_from_host(); */

		for (i = 0; i < PD_CHIP_COUNT; i++) {
			const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[i].gpio);
			if (gpio_pin_get_dt(intr) == 0) {
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
	CPRINTS("TODO Implement %s port %d", __func__, port);
}

void pd_set_new_power_request(int port)
{
	/* We probably dont need to do this since we will always request max. */
	CPRINTS("TODO Implement %s port %d", __func__, port);
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

int board_set_active_charge_port(int charge_port)
{
	prev_charge_port = charge_port;

	hook_call_deferred(&update_power_state_deferred_data, 100 * MSEC);
	CPRINTS("Updating %s port %d", __func__, charge_port);

	return EC_SUCCESS;
}

uint8_t *get_pd_version(int controller)
{
	return pd_chip_config[controller].version;
}

void set_pd_fw_update(bool is_update)
{
	firmware_update = is_update;
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
		"ERR", "POWER_ON", "APP_SETUP", "READY", "BOOTLOADER"
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
			for (p = 0; p < 2; p++) {
				CPRINTS("=====Port %d======", p);
				cypd_read_reg_block(i, CCG_PD_STATUS_REG(p), data16, 4);
				CPRINTS("PD_STATUS %s DataRole:%s PowerRole:%s Vconn:%s Partner:%s EPR:%s",
						data16[1] & BIT(2) ? "Contract" : "NoContract",
						data16[0] & BIT(6) ? "DFP" : "UFP",
						data16[1] & BIT(0) ? "Source" : "Sink",
						data16[1] & BIT(5) ? "En" : "Dis",
						data16[2] & BIT(3) ? "Un-chunked" : "Chunked",
						data16[2] & BIT(7) ? "EPR" : "Non EPR");
				cypd_read_reg8(i, CCG_TYPE_C_STATUS_REG(p), &data);
				CPRINTS("   TYPE_C_STATUS : %s %s %s %s %s",
							data & 0x1 ? "Connected" : "Not Connected",
							data & 0x2 ? "CC2" : "CC1",
							port_status[(data >> 2) & 0x7],
							data & 0x20 ? "Ra" : "NoRa",
							current_level[(data >> 6) & 0x03]);
				cypd_read_reg_block(i, CCG_CURRENT_RDO_REG(p), data16, 4);
				CPRINTS("             RDO : Current:%dmA MaxCurrent%dmA 0x%08x",
						((data16[0] + (data16[1]<<8)) & 0x3FF)*10,
						(((data16[1]>>2) + (data16[2]<<6)) & 0x3FF)*10,
						*(uint32_t *)data16);

				cypd_read_reg_block(i, CCG_CURRENT_PDO_REG(p), data16, 4);
				CPRINTS("             PDO : MaxCurrent:%dmA Voltage%dmA 0x%08x",
						((data16[0] + (data16[1]<<8)) & 0x3FF)*10,
						(((data16[1]>>2) + (data16[2]<<6)) & 0x3FF)*50,
						*(uint32_t *)data16);
				cypd_read_reg8(i, CCG_TYPE_C_VOLTAGE_REG(p), &data);
				CPRINTS("  TYPE_C_VOLTAGE : %dmV", data*100);
				cypd_read_reg16(i, CCG_PORT_INTR_STATUS_REG(p), &data);
				CPRINTS(" INTR_STATUS_REG0: 0x%02x", data);
				cypd_read_reg16(i, CCG_PORT_INTR_STATUS_REG(p)+2, &data);
				CPRINTS(" INTR_STATUS_REG1: 0x%02x", data);
				/* Flush console to avoid truncating output */
				cflush();
			}

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
			cypd_write_reg8(i, CCG_PDPORT_ENABLE_REG, 0);
			/*can take up to 650ms to discharge port for disable*/
			cypd_wait_for_ack(i, 65000);
			cypd_clear_int(i, CCG_DEV_INTR +
					  CCG_PORT0_INTR +
					  CCG_PORT1_INTR +
					  CCG_UCSI_INTR);
			usleep(50);
			CPRINTS("Full reset PD controller %d", i);
			/*
			 * see if we can talk to the PD chip yet - issue a reset command
			 * Note that we cannot issue a full reset command if the PD controller
			 * has a device attached - as it will return with an invalid command
			 * due to needing to disable all ports first.
			 */
			if (cypd_reset(i) == EC_SUCCESS) {
				CPRINTS("reset ok %d", i);
			}
		} else if (!strncmp(argv[1], "clearint", 8)) {
			cypd_clear_int(i, CCG_DEV_INTR +
					  CCG_PORT0_INTR +
					  CCG_PORT1_INTR +
					  CCG_UCSI_INTR);
		} else if (!strncmp(argv[1], "verbose", 7)) {
			verbose_msg_logging = (i != 0);
			CPRINTS("verbose=%d", verbose_msg_logging);
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
