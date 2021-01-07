/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PD chip Crypress 5525 driver
 */

#include "config.h"
#include "console.h"
#include "task.h"
#include "cypress5525.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"
#include "uart.h"
#include "util.h"
#include "chipset.h"
#include "driver/charger/isl9241.h"
#include "charger.h"
#include "charge_state.h"


#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

enum pd_chip {
	PD_CHIP_0,
	PD_CHIP_1,
	PD_CHIP_COUNT
};

static struct pd_chip_config_t pd_chip_config[] = {
	[PD_CHIP_0] = {
		.i2c_port = I2C_PORT_PD_MCU,
		.addr_flags = CYP5525_I2C_CHIP0 | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
		.state = CYP5525_STATE_POWER_ON,
		.gpio = GPIO_EC_PD_INTA_L,
	},
	[PD_CHIP_1] = {
		.i2c_port = I2C_PORT_PD_MCU,
		.addr_flags = CYP5525_I2C_CHIP1 | I2C_FLAG_ADDR16_LITTLE_ENDIAN,
		.state = CYP5525_STATE_POWER_ON,
		.gpio = GPIO_EC_PD_INTB_L,
	},
};
BUILD_ASSERT(ARRAY_SIZE(pd_chip_config) == PD_CHIP_COUNT);


enum pd_port {
	PD_PORT_0,
	PD_PORT_1,
	PD_PORT_2,
	PD_PORT_3,
	PD_PORT_COUNT
};

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


int state = CYP5525_STATE_RESET;


int pd_extpower_is_present(void)
{
	/*Todo improve this logic if we implement PPS charging*/
	int usb_c_extpower_present = 0;

	usb_c_extpower_present |= gpio_get_level(GPIO_TYPEC0_VBUS_ON_EC) ? BIT(0) : 0;
	usb_c_extpower_present |= gpio_get_level(GPIO_TYPEC1_VBUS_ON_EC) ? BIT(1) : 0;
	usb_c_extpower_present |= gpio_get_level(GPIO_TYPEC2_VBUS_ON_EC) ? BIT(2) : 0;
	usb_c_extpower_present |= gpio_get_level(GPIO_TYPEC3_VBUS_ON_EC) ? BIT(3) : 0;
	return usb_c_extpower_present;
}
static int pd_old_extpower_presence;
static void pd_extpower_deferred(void)
{
	int extpower_presence = pd_extpower_is_present();

	if (extpower_presence == pd_old_extpower_presence)
		return;
	CPRINTS("PD Source supply changed! old=0x%x, new=0x%02x",
			pd_old_extpower_presence, extpower_presence);
	pd_old_extpower_presence = extpower_presence;
	/* todo handle safety */
}
DECLARE_DEFERRED(pd_extpower_deferred);

void pd_extpower_is_present_interrupt(enum gpio_signal signal)
{
	/*Todo improve this logic if we implement PPS charging*/
	/* Trigger deferred notification of external power change */
	hook_call_deferred(&pd_extpower_deferred_data,
			1 * MSEC);
}


void pd_extpower_init(void)
{
	pd_old_extpower_presence = pd_extpower_is_present();
	gpio_enable_interrupt(GPIO_TYPEC0_VBUS_ON_EC);
	gpio_enable_interrupt(GPIO_TYPEC1_VBUS_ON_EC);
	gpio_enable_interrupt(GPIO_TYPEC2_VBUS_ON_EC);
	gpio_enable_interrupt(GPIO_TYPEC3_VBUS_ON_EC);
}

DECLARE_HOOK(HOOK_INIT, pd_extpower_init, HOOK_PRIO_INIT_EXTPOWER);

int cypd_get_active_charging_port(void)
{
	int active_port_mask = pd_extpower_is_present();
	int active_port = -1;

	switch (active_port_mask) {
	case BIT(0):
		active_port = 0;
		break;
	case BIT(1):
		active_port = 1;
		break;
	case BIT(2):
		active_port = 2;
		break;
	case BIT(3):
		active_port = 3;
		break;
	case 0:
		break;
	default:
		CPRINTS("WARNING! Danger! PD active ports are more than 1!!! 0x%02x",
				active_port_mask);
		break;
	}
	return active_port;
}
int cypd_get_adapter_power(int *voltage, int *current)
{
	int active_port;

	active_port = cypd_get_active_charging_port();
	if (!voltage  || !current) {
		return EC_ERROR_INVAL;
	}
	if (active_port < 0) {
		*voltage = *current = 0;
	}
	*voltage = pd_port_states[active_port].voltage;
	*current = pd_port_states[active_port].current;
	return EC_SUCCESS;
}

void cyp5525_update_charger(void)
{

	int voltage = 0;
	int current = 0;
	int active_port = cypd_get_active_charging_port();

	cypd_get_adapter_power(&voltage, &current);

	if (active_port >= 0) {
		CPRINTS("Updating charger to active port %d",
			active_port);
		isl9241_set_ac_prochot(0, current);
		charger_set_input_current(0, current*85/100);
	} else {
		CPRINTS("No usb-c input active. Not charging");
		charger_set_input_current(0, 0);

	}

}

int cypd_write_reg16(int controller, int reg, int data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

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

	rv = i2c_write_offset16(i2c_port, addr_flags, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_read_reg16(int controller, int reg, int *data)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

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

	rv = i2c_read_offset16(i2c_port, addr_flags, reg, data, 1);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, reg=0x%02x", __func__, controller, reg);
	return rv;
}

int cypd_get_int(int controller, int *intreg)
{
	int rv;

	rv = cypd_read_reg8(controller, CYP5525_INTR_REG, intreg);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, rv=0x%02x", __func__, controller, rv);
	return rv;
}
int cypd_clear_int(int controller, int mask)
{
	int rv;

	rv = cypd_write_reg8(controller, CYP5525_INTR_REG, mask);
	if (rv != EC_SUCCESS)
		CPRINTS("%s failed: ctrl=0x%x, mask=0x%02x", __func__, controller, mask);
	return rv;
}

/* update current when the AC status get stable */
static void cyp5225_current_update(void)
{
	cyp5525_update_charger();
}
DECLARE_HOOK(HOOK_AC_CHANGE, cyp5225_current_update, HOOK_PRIO_DEFAULT+1);

/* we need to do PD reset every power on */
int cyp5525_reset(int controller)
{
	/*
	 * Device Reset: This command is used to request the CCG device to perform a soft reset
	 * and start at the boot-loader stage again
	 * Note: need barrel AC or battery
	 */
	return cypd_write_reg16(controller, CYP5525_RESET_REG, CYP5225_RESET_CMD);
}

int cyp5225_wait_for_ack(int controller, int timeout_us)
{
	int timeout;

	timeout_us = timeout_us/10;
	/* wait for interrupt ack to be asserted */
	for (timeout = 0; timeout < timeout_us; timeout++) {
		if (gpio_get_level(pd_chip_config[controller].gpio) == 0) {
			break;
		}
		usleep(10);
	}
	/* make sure response is ok */
	if (gpio_get_level(pd_chip_config[controller].gpio) != 0) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

int cyp5225_set_power_state(int power_state)
{
	int i;
	int rv = EC_SUCCESS;

	CPRINTS("%s Setting power state to %d", __func__, power_state);

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		rv = cypd_write_reg8(i, CYP5525_SYS_PWR_STATE, power_state);
		if (rv != EC_SUCCESS)
			break;
	}
	return rv;
}

int cyp5525_setup(int controller)
{
	/* 1. CCG notifies EC with "RESET Complete event after Reset/Power up/JUMP_TO_BOOT
	 * 2. EC Reads DEVICE_MODE register does not in Boot Mode
	 * 3. CCG will enters 100ms timeout window and waits for "EC Init Complete" command
	 * 4. EC sets Source and Sink PDO mask if required
	 * 5. EC sets Event mask if required
	 * 6. EC sends EC Init Complete Command
	 */

	int rv, data, i;
	#define CYPD_SETUP_CMDS_LEN  4
	struct {
		int reg;
		int value;
		int status_reg;
	} const cypd_setup_cmds[] = {
		{ CYP5525_EVENT_MASK_REG(0), 0xffff, CYP5525_PORT0_INTR},	/* Set the port 0 event mask */
		{ CYP5525_EVENT_MASK_REG(1), 0xffff, CYP5525_PORT1_INTR },	/* Set the port 1 event mask */
		{ CYP5525_PD_CONTROL_REG(0), CYPD_PD_CMD_EC_INIT_COMPLETE, CYP5525_PORT0_INTR },	/* EC INIT Complete */
		{ CYP5525_PD_CONTROL_REG(1), CYPD_PD_CMD_EC_INIT_COMPLETE, CYP5525_PORT1_INTR },	/* EC INIT Complete */
	};
	BUILD_ASSERT(ARRAY_SIZE(cypd_setup_cmds) == CYPD_SETUP_CMDS_LEN);

	/* Make sure the interrupt is not asserted before we start */
	if (gpio_get_level(pd_chip_config[controller].gpio) == 0) {
		rv = cypd_get_int(controller, &data);
		CPRINTS("%s int already pending 0x%04x", __func__, data);
		cypd_clear_int(controller, CYP5525_DEV_INTR+CYP5525_PORT0_INTR+CYP5525_PORT1_INTR+CYP5525_UCSI_INTR);
	}
	for (i = 0; i < CYPD_SETUP_CMDS_LEN; i++) {
		rv = cypd_write_reg16(controller, cypd_setup_cmds[i].reg,
		cypd_setup_cmds[i].value);
		if (rv != EC_SUCCESS) {
			CPRINTS("%s command: 0x%04x failed", __func__, cypd_setup_cmds[i].reg);
			return EC_ERROR_INVAL;
		}
		/* wait for interrupt ack to be asserted */
		if (cyp5225_wait_for_ack(controller, 5000) != EC_SUCCESS) {
			CPRINTS("%s timeout on interrupt", __func__);
			return EC_ERROR_INVAL;
		}

		/* clear cmd ack */
		cypd_clear_int(controller, cypd_setup_cmds[i].status_reg);
	}
	return EC_SUCCESS;
}

void cyp5525_get_sink_power(int controller, int port)
{
	int rv;
	uint8_t data2[4];
	int active_current = 0;
	int active_voltage = 0;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16_block(i2c_port, addr_flags, CYP5525_PD_STATUS_REG(port), data2, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_PD_STATUS_REG failed");

	if ((data2[1] & CYP5525_PD_CONTRACT_STATE) == CYP5525_PD_CONTRACT_STATE) {
		rv = i2c_read_offset16_block(i2c_port, addr_flags, CYP5525_CURRENT_PDO_REG(port), data2, 4);
		active_current = (data2[0] + ((data2[1] & 0x3) << 8)) * 10;
		active_voltage = (((data2[1] & 0xFC) >> 2) + ((data2[2] & 0xF) << 6)) * 50;
		CPRINTS("C%d, current:%d mA, voltage:%d mV", port, active_current, active_voltage);
		pd_port_states[(controller << 1) + port].current = active_current;
		pd_port_states[(controller << 1) + port].voltage = active_voltage;
	}
}

void cyp5525_port_int(int controller, int port)
{
	int rv;
	uint8_t data2[4];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16_block(i2c_port, addr_flags, CYP5525_PORT_PD_RESPONSE_REG(port), data2, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("PORT_PD_RESPONSE_REG failed");
		print_pd_response_code(controller,
		port,
		data2[0],
		data2[1]);

	switch (data2[0]) {
	case CYPD_RESPONSE_PORT_DISCONNECT:
		CPRINTS("CYPD_RESPONSE_PORT_DISCONNECT");
		pd_port_states[(controller << 1) + port].current = 0;
		pd_port_states[(controller << 1) + port].voltage = 0;
		cyp5525_update_charger();
		break;
	case CYPD_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE:
		CPRINTS("CYPD_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE");
		/*todo we can probably clean this up to remove some of this*/
		cyp5525_get_sink_power(controller, port);
		cyp5525_update_charger();
		break;
	case CYPD_RESPONSE_PORT_CONNECT:
		CPRINTS("CYPD_RESPONSE_PORT_CONNECT");
		break;
	}
}

int cyp5525_device_int(int controller)
{
	int data;

	CPRINTS("INTR_REG TODO Handle Device");
	if (cypd_read_reg16(controller, CYP5525_RESPONSE_REG, &data) == EC_SUCCESS) {
		print_pd_response_code(controller,
		-1,
		data & 0xff,
		data>>8);
	} else {
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;

}
void cyp5525_interrupt(int controller)
{
	int data;
	int rv;
	int clear_mask = 0;
	rv = cypd_get_int(controller, &data);
	if (rv != EC_SUCCESS) {
		return;
	}
	switch (pd_chip_config[controller].state) {
	case CYP5525_STATE_READY:

		/*CPRINTS("INTR_REG read value: 0x%02x", data);*/

		/* Process device interrupt*/
		if (data & CYP5525_DEV_INTR) {
			cyp5525_device_int(controller);
			clear_mask |= CYP5525_DEV_INTR;
		}
		if (data & CYP5525_PORT0_INTR) {
			/* */
			cyp5525_port_int(controller, 0);
			clear_mask |= CYP5525_PORT0_INTR;
		}

		if (data & CYP5525_PORT1_INTR) {
			/* */
			cyp5525_port_int(controller, 1);
			clear_mask |= CYP5525_PORT1_INTR;
		}
		if (data & CYP5525_UCSI_INTR) {
			/* */
			CPRINTS("INTR_REG TODO Handle UCSI");
			clear_mask |= CYP5525_UCSI_INTR;
		}
		break;

	case CYP5525_STATE_POWER_ON:
		if (data & CYP5525_DEV_INTR && 
				cypd_read_reg16(controller, CYP5525_RESPONSE_REG, &data) == EC_SUCCESS) {

			CPRINTS("RESPONSE: Code: 0x%02x", data);
			if ((data & 0xFF) == CYPD_RESPONSE_RESET_COMPLETE) {
				CPRINTS("CYPD %d boot ok", controller);
				pd_chip_config[controller].state = CYP5525_STATE_RESET;
			}
			clear_mask = CYP5525_DEV_INTR;
		} else {
			/* There may be port interrupts pending from previous boot so clear them here */
			clear_mask = data;
		}

		break;
	case CYP5525_STATE_BOOTING:
		if (data & CYP5525_DEV_INTR &&
				cypd_read_reg16(controller, CYP5525_RESPONSE_REG, &data) == EC_SUCCESS) {

			if ((data & 0xFF) == CYPD_RESPONSE_RESET_COMPLETE) {
				CPRINTS("CYPD %d boot ok", controller);
				pd_chip_config[controller].state = CYP5525_STATE_RESET;
			} else {
				CPRINTS("CYPD %d boot error 0x%02x", controller, data);
				/* Try again */
				pd_chip_config[controller].state = CYP5525_STATE_POWER_ON;
			}
			clear_mask = CYP5525_DEV_INTR;
		}
		break;

	case CYP5525_STATE_I2C_RESET:
		if (data & CYP5525_DEV_INTR &&
				cypd_read_reg16(controller, CYP5525_RESPONSE_REG, &data) == EC_SUCCESS) {

			if ((data & 0xFF) == CYPD_RESPONSE_SUCCESS) {
				CPRINTS("CYPD %d i2c reset ok", controller);
				pd_chip_config[controller].state = CYP5525_STATE_RESET;
			} else {
				CPRINTS("CYPD %d boot error 0x%02x", controller, data);
			}
			clear_mask = CYP5525_DEV_INTR;
		}

		break;
	default:
		CPRINTS("Got interrupt from PD but in 0x%02x state!", pd_chip_config[controller].state);
		clear_mask = data;
		break;
	}

	cypd_clear_int(controller, clear_mask);

}

#define CYPD_PROCESS_CONTROLLER_AC_PRESENT BIT(31)
#define CYPD_PROCESS_CONTROLLER_S0 BIT(30)
#define CYPD_PROCESS_CONTROLLER_S3 BIT(29)
#define CYPD_PROCESS_CONTROLLER_S4 BIT(28)
#define CYPD_PROCESS_CONTROLLER_S5 BIT(27)


static uint8_t cypd_int_task_id;

void schedule_deferred_cypd_interrupt(const int controller)
{
	task_set_event(cypd_int_task_id, controller, 0);
}

void pd_chip_interrupt(enum gpio_signal signal)
{
	int i;

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		if (signal == pd_chip_config[i].gpio) {
			schedule_deferred_cypd_interrupt(1<<i);
		}
	}
	if (signal == GPIO_AC_PRESENT_PD_L) {
		schedule_deferred_cypd_interrupt(CYPD_PROCESS_CONTROLLER_AC_PRESENT);
	}
}


/* Called on AP S5 -> S3 transition */
static void pd_enter_s3(void)
{
	task_set_event(cypd_int_task_id, CYPD_PROCESS_CONTROLLER_S3, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP,
		pd_enter_s3,
		HOOK_PRIO_DEFAULT);

DECLARE_HOOK(HOOK_CHIPSET_SUSPEND,
		pd_enter_s3,
		HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void pd_enter_s5(void)
{
	task_set_event(cypd_int_task_id, CYPD_PROCESS_CONTROLLER_S5, 0);

}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN,
		pd_enter_s5,
		HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S0 transition */
static void pd_enter_s0(void)
{
	task_set_event(cypd_int_task_id, CYPD_PROCESS_CONTROLLER_S0, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pd_enter_s0,
	     HOOK_PRIO_DEFAULT);






void cypd_interrupt_handler_task(void *p)
{
	int loop_count = 0;
	int i, data, setup_pending;
	int gpio_asserted = 0;
	timestamp_t now;
	timestamp_t expire_time;
	cypd_int_task_id = task_get_current();
	/* wait for pd controller to init*/
	msleep(25);
	now = get_time();
	expire_time.val = now.val + 1000*MSEC;
	while (!timestamp_expired(expire_time, &now)) {
		setup_pending = 0;
		cflush();
		for (i = 0; i < PD_CHIP_COUNT; i++) {
			switch (pd_chip_config[i].state) {
			case CYP5525_STATE_POWER_ON:
				if (gpio_get_level(pd_chip_config[i].gpio) == 0) {
					cyp5525_interrupt(i);
				} else {
					if (charge_get_state() != PWR_STATE_ERROR){
						/*
						* Disable all ports - otherwise reset command is not guaranteed to work
						* Try to coast on bulk capacitance on EC power supply while PD controller resets
						* if there is no battery attached.
						* TODO FIXME
						*/
						if (cypd_write_reg8(i, CYP5525_PDPORT_ENABLE_REG, 0) == EC_SUCCESS) {
							/*can take up to 650ms to discharge port for disable*/
							cyp5225_wait_for_ack(i, 65000);
							cypd_clear_int(i, CYP5525_DEV_INTR+CYP5525_PORT0_INTR+CYP5525_PORT1_INTR+CYP5525_UCSI_INTR);
							usleep(50);
							CPRINTS("Full reset PD controller %d", i);
							/*
							* see if we can talk to the PD chip yet - issue a reset command
							* Note that we cannot issue a full reset command if the PD controller
							* has a device attached - as it will return with an invalid command
							* due to needing to disable all ports first.
							*/
							if (cyp5525_reset(i) == EC_SUCCESS) {
								pd_chip_config[i].state = CYP5525_STATE_BOOTING;
							} else {
								CPRINTS("PD Failed to issue reset command %d", i);
							}
						}
					} else {
						CPRINTS("No battery - partial PD reset");
						if (cypd_write_reg16(i, CYP5525_RESET_REG, CYP5225_RESET_CMD_I2C) == EC_SUCCESS) {
							pd_chip_config[i].state = CYP5525_STATE_I2C_RESET;
						}
					}
				}
				break;

			case CYP5525_STATE_I2C_RESET:
			case CYP5525_STATE_BOOTING:
				/* The PD controller is resetting, wait for it to boot */
				if (gpio_get_level(pd_chip_config[i].gpio) == 0) {
					cyp5525_interrupt(i);
				}
				break;

			case CYP5525_STATE_RESET:
				/* check what mode we are in */
				if (cypd_read_reg8(i, CYP5525_DEVICE_MODE, &data) == EC_SUCCESS) {
					if ((data & 0x03) == 0x00) {
						pd_chip_config[i].state = CYP5525_STATE_BOOTLOADER;
						CPRINTS("CYPD %d is in bootloader 0x%04x", i, data);
						if (cypd_read_reg16(i, CYP5525_BOOT_MODE_REASON, &data) == EC_SUCCESS) {
							CPRINTS("CYPD bootloader reason 0x%02x", data);

						}

					} else {
						pd_chip_config[i].state = CYP5525_STATE_SETUP;
					}
				}
				break;
			case CYP5525_STATE_SETUP:
				if (cyp5525_setup(i) == EC_SUCCESS) {
					cyp5525_get_sink_power(i, 0);
					cyp5525_get_sink_power(i, 1);
					gpio_enable_interrupt(pd_chip_config[i].gpio);
					CPRINTS("CYPD %d Ready!", i);
					pd_chip_config[i].state = CYP5525_STATE_READY;
				}
				break;
			case CYP5525_STATE_BOOTLOADER:
				if (cypd_read_reg8(i, CYP5525_DEVICE_MODE, &data) == EC_SUCCESS) {
					if ((data & 0x03) != 0x00) {
						CPRINTS("CYPD %d is in FW %d", i, data & 0x03);
						pd_chip_config[i].state = CYP5525_STATE_SETUP;
					}
				}
				break;
			case CYP5525_STATE_READY:

				break;
			default:
				break;
			}
			/* if we are done with the setup state machine */
			if (pd_chip_config[i].state != CYP5525_STATE_BOOTLOADER &&
				pd_chip_config[i].state != CYP5525_STATE_READY) {
					setup_pending = 1;
				}
		}
		if (setup_pending == 0) {
			break;
		}
		msleep(1);
		/*
		 * If we issued a reset command, then the PD controller needs to get through bootloader wait time
		 * before we issue additional commands 
		 */
		if (pd_chip_config[0].state == CYP5525_STATE_BOOTING || pd_chip_config[1].state == CYP5525_STATE_BOOTING) {
			msleep(60);
		}
		now = get_time();
	}
	cyp5525_update_charger();
	CPRINTS("CYPD Finished setup");


	for (i = 0; i < PD_CHIP_COUNT; i++) {
		if (gpio_get_level(pd_chip_config[i].gpio) == 0) {
		   schedule_deferred_cypd_interrupt(1<<i);
		}
	}
	while (1) {
		const int evt = task_wait_event(-1);
		loop_count = 0;
		if (evt) {
			if (evt & CYPD_PROCESS_CONTROLLER_AC_PRESENT) {
				CPRINTS("GPIO_AC_PRESENT_PD_L changed: value: 0x%02x", gpio_get_level(GPIO_AC_PRESENT_PD_L));
			}
			if (evt & CYPD_PROCESS_CONTROLLER_S0) {
				cyp5225_set_power_state(CYP5525_POWERSTATE_S0);
			}
			if (evt & CYPD_PROCESS_CONTROLLER_S3) {
				cyp5225_set_power_state(CYP5525_POWERSTATE_S3);
			}
			if (evt & CYPD_PROCESS_CONTROLLER_S5) {
				cyp5225_set_power_state(CYP5525_POWERSTATE_S5);
			}
			gpio_asserted = 0;
			for (i = 0; i < PD_CHIP_COUNT; i++) {
				if (gpio_get_level(pd_chip_config[i].gpio) == 0) {
					gpio_asserted = 1;
				}
			}
			while (gpio_asserted) {
				gpio_asserted = 0;
				for (i = 0; i < PD_CHIP_COUNT; i++) {
					if (gpio_get_level(pd_chip_config[i].gpio) == 0) {
						gpio_asserted = 1;
						cyp5525_interrupt(i);
					}
				}
				/* wait for PD controller to deassert the interrupt line */
				usleep(50);
				cflush(); /*TODO REMOVE ME*/
				loop_count++;
				if (loop_count > 100) {
					CPRINTS("WARNING: cypd_interrupt_handler_task has exceeded loop count!");
					for (i = 0; i < PD_CHIP_COUNT; i++) {
						CPRINTS("Controller %d State: %d, Interrupt %d", i, pd_chip_config[i].state, gpio_get_level(pd_chip_config[i].gpio));
					}
					break;
				}
			}
		}
	}
}

int cypd_get_active_power_budget(void)
{
	/* TODO:
	 * We need to select the max power port, current design does not disable other port
	 */
	int power = 60;
	int voltage = 0;
	int current = 0;
	
	cypd_get_adapter_power(&voltage, &current);
	power = (voltage * current/(1000*1000));

	return power;
}

int cypd_get_pps_power_budget(void)
{
	/* TODO:
	 * Implement PPS function and get pps power budget
	 */
	int power = 0;

	return power;
}

void print_pd_response_code(uint8_t controller, uint8_t port, uint8_t id, int len)
{
	const char *code;
#ifdef PD_VERBOSE_LOGGING
	static const char *response_codes[256] = {
		[0x00] = "NONE",
		[0x02] = "SUCCESS",
		[0x03] = "FLASH_DATA_AVAILABLE",
		[0x05] = "INVALID_COMMAND",
		[0x06] = "INVALID_STATE",
		[0x07] = "FLASH_UPDATE_FAILED",
		[0x08] = "INVALID_FW",
		[0x09] = "INVALID_ARGUMENTS",
		[0x0A] = "NOT_SUPPORTED",
		[0x0C] = "TRANSACTION_FAILED",
		[0x0D] = "PD_COMMAND_FAILED",
		[0x0F] = "UNDEFINED_ERROR",
		[0x10] = "READ_PDO_DATA",
		[0x11] = "CMD_ABORTED",
		[0x12] = "PORT_BUSY",
		[0x13] = "MINMAX_CURRENT",
		[0x14] = "EXT_SRC_CAP",
		[0x18] = "DID_RESPONSE",
		[0x19] = "SVID_RESPONSE",
		[0x1A] = "DISCOVER_MODE_RESPONSE",
		[0x1B] = "CABLE_COMM_NOT_ALLOWED",
		[0x1C] = "EXT_SNK_CAP",
		[0x40] = "FWCT_IDENT_INVALID",
		[0x41] = "FWCT_INVALID_GUID",
		[0x42] = "FWCT_INVALID_VERSION",
		[0x43] = "HPI_CMD_INVALID_SEQ",
		[0x44] = "FWCT_AUTH_FAILED",
		[0x45] = "HASH_FAILED",
		[0x80] = "RESET_COMPLETE",
		[0x81] = "MESSAGE_QUEUE_OVERFLOW",
		[0x82] = "OVER_CURRENT",
		[0x83] = "OVER_VOLT",
		[0x84] = "PORT_CONNECT",
		[0x85] = "PORT_DISCONNECT",
		[0x86] = "PD_CONTRACT_NEGOTIATION_COMPLETE",
		[0x87] = "SWAP_COMPLETE",
		[0x8A] = "PS_RDY_MSG_PENDING",
		[0x8B] = "GOTO_MIN_PENDING",
		[0x8C] = "ACCEPT_MSG_RX",
		[0x8D] = "REJECT_MSG_RX",
		[0x8E] = "WAIT_MSG_RX",
		[0x8F] = "HARD_RESET_RX",
		[0x90] = "VDM_RX",
		[0x91] = "SOURCE_CAP_MSG_RX",
		[0x92] = "SINK_CAP_MSG_RX",
		[0x93] = "USB4_DATA_RESET_RX",
		[0x94] = "USB4_DATA_RESET_COMPLETE",
		[0x95] = "USB4_ENTRY_COMPLETE",
		[0x9A] = "HARD_RESET_SENT",
		[0x9B] = "SOFT_RESET_SENT",
		[0x9C] = "CABLE_RESET_SENT",
		[0x9D] = "SOURCEDISABLED",
		[0x9E] = "SENDER_RESPONSE_TIMEOUT",
		[0x9F] = "NO_VDM_RESPONSE_RX",
		[0xA0] = "UNEXPECTED_VOLTAGE",
		[0xA1] = "TYPE_C_ERROR_RECOVERY",
		[0xA2] = "BATTERY_STATUS_RX",
		[0xA3] = "ALERT_RX",
		[0xA4] = "UNSUPPORTED_MSG_RX",
		[0xA6] = "EMCA_DETECTED",
		[0xA7] = "CABLE_DISCOVERY_FAILED",
		[0xAA] = "RP_CHANGE_DETECTED",
		[0xAC] = "EXT_MSG_SOP_RX",
		[0xB0] = "ALT_MODE_EVENT",
		[0xB1] = "ALT_MODE_HW_EVENT",
		[0xB4] = "EXT_SOP1_RX",
		[0xB5] = "EXT_SOP2_RX",
		[0xB6] = "OVER_TEMP",
		[0xB8] = "HARDWARE_ERROR",
		[0xB9] = "VCONN_OCP_ERROR",
		[0xBA] = "CC_OVP_ERROR",
		[0xBB] = "SBU_OVP_ERROR",
		[0xBC] = "VBUS_SHORT_ERROR",
		[0xBD] = "REVERSE_CURRENT_ERROR",
		[0xBE] = "SINK_STANDBY"
	};
	code = response_codes[id];
	if (code == NULL)
		code = "UNKNOWN";
#else /*PD_VERBOSE_LOGGING*/
	code = "";
#endif /*PD_VERBOSE_LOGGING*/

	CPRINTS("PD Controller %d Port %d  Code 0x%02x %s %s Len: 0x%02x",
	controller,
	port,
	id,
	code,
	id & 0x80 ? "Response" : "Event",
	len);
}

static int cmd_cypd_get_status(int argc, char **argv)
{
	int i, data;
	char *e;

	static const char * const mode[] = {"Boot", "FW1", "FW2", "Invald"};
	static const char * const port_status[] = {"Nothing", "Sink", "Source", "Debug", "Audio", "Powered Acc", "Unsupported", "Invalid"};

	CPRINTS("AC_PRESENT_PD value: %d", gpio_get_level(GPIO_AC_PRESENT_PD_L));
	for (i = 0; i < PD_CHIP_COUNT; i++) {
		CPRINTS("PD%d INT value: %d", i, gpio_get_level(pd_chip_config[i].gpio));
	}

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		if (i < PD_CHIP_COUNT) {
			cypd_read_reg8(i, CYP5525_DEVICE_MODE, &data);
			CPRINTS("CYP5525_DEVICE_MODE: 0x%02x %s", data, mode[data & 0x03]);

			cypd_read_reg8(i, CYP5525_INTR_REG, &data);
			CPRINTS("CYP5525_INTR_REG: 0x%02x", data);
			CPRINTS("				: %s", data & CYP5525_DEV_INTR ? "CYP5525_DEV_INTR" : "");
			CPRINTS("				: %s", data & CYP5525_PORT0_INTR ? "CYP5525_PORT0_INTR" : "");
			CPRINTS("				: %s", data & CYP5525_PORT1_INTR ? "CYP5525_PORT1_INTR" : "");
			CPRINTS("				: %s", data & CYP5525_UCSI_INTR ? "CYP5525_UCSI_INTR" : "");

			cypd_read_reg16(i, CYP5525_RESPONSE_REG, &data);
			CPRINTS("CYP5525_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg16(i, CYP5525_PORT_PD_RESPONSE_REG(0), &data);
			CPRINTS("CYP5525_PORT0_PD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg16(i, CYP5525_PORT_PD_RESPONSE_REG(1), &data);
			CPRINTS("CYP5525_PORT1_PD_RESPONSE_REG: 0x%02x", data);


			cypd_read_reg8(i, CYP5525_BOOT_MODE_REASON, &data);
			CPRINTS("CYP5525_BOOT_MODE_REASON: 0x%02x", data);

			cypd_read_reg16(i, CYP5525_SILICON_ID, &data);
			CPRINTS("CYP5525_SILICON_ID: 0x%04x", data);

			cypd_read_reg8(i, CYP5525_PDPORT_ENABLE_REG, &data);
			CPRINTS("CYP5525_PDPORT_ENABLE_REG: 0x%04x", data);

			cypd_read_reg8(i, CYP5525_POWER_STAT, &data);
			CPRINTS("CYP5525_POWER_STAT: 0x%02x", data);

			cypd_read_reg8(i, CYP5525_SYS_PWR_STATE, &data);
			CPRINTS("CYP5525_SYS_PWR_STATE: 0x%02x", data);

			cypd_read_reg8(i, CYP5525_TYPE_C_STATUS_REG(0), &data);
			CPRINTS("  TYPE_C_STATUS0 : %s", data & 0x1 ? "Connected" : "Not Connected");
			CPRINTS("  TYPE_C_STATUS0 : %s", data & 0x2 ? "CC2" : "CC1");
			CPRINTS("  TYPE_C_STATUS0 : %s", port_status[(data >> 2) & 0x3]);

			cypd_read_reg16(i, CYP5525_PORT_INTR_STATUS_REG(0), &data);
			CPRINTS("PORT0_INTR_STATUS_REG0: 0x%02x", data);
			cypd_read_reg16(i, CYP5525_PORT_INTR_STATUS_REG(0)+2, &data);
			CPRINTS("PORT0_INTR_STATUS_REG1: 0x%02x", data);

			cypd_read_reg8(i, CYP5525_TYPE_C_VOLTAGE_REG(0), &data);
			CPRINTS("  TYPE_C_VOLTAGE0 : %dmV", data*100);


			cypd_read_reg8(i, CYP5525_TYPE_C_STATUS_REG(1), &data);
			CPRINTS("  TYPE_C_STATUS1 : %s", data & 0x1 ? "Connected" : "Not Connected");
			CPRINTS("  TYPE_C_STATUS1 : %s", data & 0x2 ? "CC2" : "CC1");
			CPRINTS("  TYPE_C_STATUS1 : %s", port_status[(data >> 2) & 0x3]);

			cypd_read_reg16(i, CYP5525_PORT_INTR_STATUS_REG(1), &data);
			CPRINTS("PORT1_INTR_STATUS_REG0: 0x%02x", data);
			cypd_read_reg16(i, CYP5525_PORT_INTR_STATUS_REG(1)+2, &data);
			CPRINTS("PORT1_INTR_STATUS_REG1: 0x%02x", data);

			cypd_read_reg8(i, CYP5525_TYPE_C_VOLTAGE_REG(1), &data);
			CPRINTS("  TYPE_C_VOLTAGE1 : %dmV", data*100);

		}

	} else { /* Otherwise print them all */

	}

	/* Flush console to avoid truncating output */
	cflush();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cypdstatus, cmd_cypd_get_status,
			"[number]",
			"Get Cypress PD controller status");


static int cmd_cypd_control(int argc, char **argv)
{
	int i, enable;
	char *e;

	if (argc == 3) {
		i = strtoi(argv[1], &e, 0);
		if (*e || i >= PD_CHIP_COUNT)
			return EC_ERROR_PARAM1;

		if (!parse_bool(argv[2], &enable))
			return EC_ERROR_PARAM2;

		if (enable)
			gpio_enable_interrupt(pd_chip_config[i].gpio);
		else
			gpio_disable_interrupt(pd_chip_config[i].gpio);

	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cypdctl, cmd_cypd_control,
			"[number] [enable/disable]",
			"Set if handling is active for controller");
