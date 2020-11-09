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

int cyp5525_setup(int controller)
{
	/* 1. CCG notifies EC with "RESET Complete event after Reset/Power up/JUMP_TO_BOOT
	 * 2. EC Reads DEVICE_MODE register does not in Boot Mode
	 * 3. CCG will enters 100ms timeout window and waits for "EC Init Complete" command
	 * 4. EC sets Source and Sink PDO mask if required
	 * 5. EC sets Event mask if required
	 * 6. EC sends EC Init Complete Command
	 */

	int rv, data, i, timeout;
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
		for (timeout = 0; timeout < 10; timeout++) {
			if (gpio_get_level(pd_chip_config[controller].gpio) == 0) {
				break;
			}
			usleep(50);
		}
		/* make sure response is ok */
		if (gpio_get_level(pd_chip_config[controller].gpio) != 0) {
			CPRINTS("%s timeout on interrupt", __func__);
			return EC_ERROR_INVAL;
		}
		/* clear cmd ack */
		cypd_clear_int(controller, cypd_setup_cmds[i].status_reg);
	}
	return EC_SUCCESS;
}

void cyp5525_port_int(int controller, int port)
{
	int rv;
	int data;
	uint8_t data2[4];
	int active_current = 0;
	int active_voltage = 0;
	int state = CYP5525_DEVICE_DETACH;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	CPRINTS("C%d interrupt!", port);

	/*
	* TODO: should we need to check the PD response register?
	* i2c_read_offset16(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_PD_RESPONSE_REG), &data, 2);
	* CPRINTS("RESPONSE REG read value: 0x%02x", data);
	*/
	rv = cypd_read_reg8(controller, CYP5525_TYPE_C_STATUS_REG(port), &data);
	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_TYPE_C_STATUS_REG failed");

	CPRINTS("DEVICE_MODE read value: 0x%02x", data);

	if ((data & CYP5525_PORT_CONNECTION) == CYP5525_PORT_CONNECTION) {
		state = CYP5525_DEVICE_ATTACH;
	}

	if (state == CYP5525_DEVICE_ATTACH) {
		/* Read the RDO if attach adaptor */

		rv = i2c_read_offset16_block(i2c_port, addr_flags, CYP5525_PD_STATUS_REG(port), data2, 4);
		if (rv != EC_SUCCESS)
			CPRINTS("CYP5525_PD_STATUS_REG failed");

		if ((data2[1] & CYP5525_PD_CONTRACT_STATE) == CYP5525_PD_CONTRACT_STATE) {
			state = CYP5525_DEVICE_ATTACH_WITH_CONTRACT;
		}

		if (state == CYP5525_DEVICE_ATTACH_WITH_CONTRACT) {
			rv = i2c_read_offset16_block(i2c_port, addr_flags, CYP5525_CURRENT_PDO_REG(port), data2, 4);
			active_current = (data2[0] + ((data2[1] & 0x3) << 8)) * 10;
			active_voltage = (((data2[1] & 0xFC) >> 2) + ((data2[2] & 0xF) << 6)) * 50;
			CPRINTS("C%d, current:%d mA, voltage:%d mV", port, active_current, active_voltage);
			/*i2c_read_offset16_block(I2C_PORT_PD_MCU, CYP5525_ADDRESS_FLAG, offsetL2M(CYP5525_CURRENT_RDO_REG(port)), &data2, 4);*/
			/* TODO: charge_manager to switch the VBUS */
		}
	}
}

int cyp5525_device_int(int controller)
{
	int data;

	CPRINTS("INTR_REG TODO Handle Device");
	if (cypd_read_reg16(controller, CYP5525_RESPONSE_REG, &data) == EC_SUCCESS) {
		CPRINTS("RESPONSE: Code: 0x%02x", data & 0x7F);
		CPRINTS("RESPONSE: Length: 0x%02x", data>>8);
		CPRINTS("RESPONSE: msg type %s", data & 0x80 ? "Async" : "Response Ack");
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

	if (pd_chip_config[controller].state == CYP5525_STATE_READY) {
		rv = cypd_get_int(controller, &data);
		if (rv != EC_SUCCESS) {
			return;
		}
		CPRINTS("INTR_REG read value: 0x%02x", data);

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
		cypd_clear_int(controller, clear_mask);
	} else {
		/*rv = cypd_get_int(controller, &data);
		* CPRINTS("INTR_REG %d state, read value: 0x%02x", pd_chip_config[controller].state, data);
		* rv = cypd_clear_int(controller, data);
		* */
	}
}

#define CYPD_PROCESS_CONTROLLER_AC_PRESENT BIT(31)

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



void cypd_interrupt_handler_task(void *p)
{
	int loop_count = 0;
	int i, data, setup_pending;
	int gpio_asserted = 0;
	timestamp_t now;
	timestamp_t expire_time;
	cypd_int_task_id = task_get_current();

	now = get_time();
	expire_time.val = now.val + 500*MSEC;
	while (!timestamp_expired(expire_time, &now)) {
		setup_pending = 0;
		for (i = 0; i < PD_CHIP_COUNT; i++) {
			switch (pd_chip_config[i].state) {
			case CYP5525_STATE_POWER_ON:
				if (gpio_get_level(pd_chip_config[i].gpio) == 0) {
					if (cypd_get_int(i, &data) == EC_SUCCESS) {
						CPRINTS("CYP5525_STATE_POWER_ON int already pending 0x%04x", data);
						cypd_clear_int(i, CYP5525_DEV_INTR+CYP5525_PORT0_INTR+CYP5525_PORT1_INTR+CYP5525_UCSI_INTR);
					}
				} else {
					/*
						* Disable all ports - otherwise reset command is not guarenteed to work
						* Try to coast on bulk capacitance on EC power supply while PD controller resets
						* if there is no battery attached.
						* TODO FIXME
					*/
					/*if(cypd_write_reg8(i, CYP5525_PDPORT_ENABLE_REG, 0) == EC_SUCCESS){
					*
					*}
					*/
					/* see if we can talk to the PD chip yet - issue a reset command
						* Note that we cannot issue a full reset command if the PD controller
						* has a device attached - as it will return with an invalid command
						* due to needing to disable all ports first.
						* */
					if (cypd_write_reg16(i, CYP5525_RESET_REG, CYP5225_RESET_CMD_I2C) == EC_SUCCESS) {
						pd_chip_config[i].state = CYP5525_STATE_BOOTING;
					}
				}
				break;
			case CYP5525_STATE_BOOTING:
				/* The PD controller is resetting, wait for it to boot */
				if (gpio_get_level(pd_chip_config[i].gpio) == 0) {
					if (cypd_get_int(i, &data) == EC_SUCCESS) {
						/* Check we have a pending DEVICE interrupt, Check response register */
						if (data & CYP5525_DEV_INTR && \
								cypd_read_reg16(i, CYP5525_RESPONSE_REG, &data) == EC_SUCCESS) {

							if ((data & 0xFF) == CYPD_RESPONSE_SUCCESS) {
								pd_chip_config[i].state = CYP5525_STATE_RESET;
							} else {
								CPRINTS("CYPD %d boot error 0x%02x", i, data);
							}
							cypd_clear_int(i, CYP5525_DEV_INTR);
						}
					}
				}
				break;
			case CYP5525_STATE_RESET:
				/* check what mode we are in */
				if (cypd_read_reg16(i, CYP5525_DEVICE_MODE, &data) == EC_SUCCESS) {
					if ((data & 0x03) == 0x00) {
						pd_chip_config[i].state = CYP5525_STATE_BOOTLOADER;
					} else {
						pd_chip_config[i].state = CYP5525_STATE_SETUP;
					}
				}
				break;
			case CYP5525_STATE_SETUP:
				if (cyp5525_setup(i) == EC_SUCCESS) {
					gpio_enable_interrupt(pd_chip_config[i].gpio);
					CPRINTS("CYPD %d Ready!", i);
					pd_chip_config[i].state = CYP5525_STATE_READY;
				}
				break;
			case CYP5525_STATE_BOOTLOADER:
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
		now = get_time();
	}

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
				cflush(); /*TODO REMOVE ME*/
				loop_count++;
				if (loop_count > 100) {
					CPRINTS("WARNING: cypd_interrupt_handler_task has exceeded loop count!");
					break;
				}
			}
		}
	}
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