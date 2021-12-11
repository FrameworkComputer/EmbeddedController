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
#include "ucsi.h"
#include "util.h"
#include "chipset.h"
#include "driver/charger/isl9241.h"
#include "charger.h"
#include "charge_state.h"
#include "charge_manager.h"
#include "usb_tc_sm.h"
#include "usb_pd.h"
#include "usb_emsg.h"
#include "power.h"
#include "cpu_power.h"
#include "power_sequence.h"
#include "extpower.h"
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

#define BATT_CHARGING	0x00
#define BATT_DISCHARGING	0x01
#define BATT_IDLE	0x10

#define BATT_STATUS_REF	1
#define CHUNKED_MASK	0x80

#define PRODUCT_ID	0x0001
#define VENDOR_ID	0x32ac

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

struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
struct extended_msg tx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];


bool verbose_msg_logging;
static bool firmware_update;

static bool system_power_present;

void set_pd_fw_update(bool update)
{
	firmware_update = update;
}

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

int cypd_write_reg_block(int controller, int reg, void *data, int len)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

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

int cypd_read_reg_block(int controller, int reg, void *data, int len)
{
	int rv;
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

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


int cypd_write_reg8_wait_ack(int controller, int reg, int data)
{
	int rv = EC_SUCCESS;
	int intr_status;
	rv = cypd_write_reg8(controller, reg, data);
	if (rv != EC_SUCCESS)
		CPRINTS("Write Reg8 0x%x fail!", reg);

	if (cyp5225_wait_for_ack(controller, 100000) != EC_SUCCESS) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	rv = cypd_get_int(controller, &intr_status);
	if (intr_status & CYP5525_DEV_INTR) {
		cypd_clear_int(controller, CYP5525_DEV_INTR);
	}
	return rv;
}
int cypd_write_reg16_wait_ack(int controller, int reg, int data)
{
	int rv = EC_SUCCESS;
	int intr_status;
	rv = cypd_write_reg16(controller, reg, data);
	if (rv != EC_SUCCESS)
		CPRINTS("Write Reg8 0x%x fail!", reg);

	if (cyp5225_wait_for_ack(controller, 100*MSEC) != EC_SUCCESS) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	rv = cypd_get_int(controller, &intr_status);
	if (intr_status & CYP5525_DEV_INTR) {
		cypd_clear_int(controller, CYP5525_DEV_INTR);
	}
	usleep(50);
	return rv;
}

int cypd_set_power_state(int power_state)
{
	int i;
	int rv = EC_SUCCESS;

	CPRINTS("%s pwr state %d", __func__, power_state);

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		rv = cypd_write_reg8_wait_ack(i, CYP5525_SYS_PWR_STATE, power_state);
		if (rv != EC_SUCCESS)
			break;
	}
	return rv;
}

static bool charger_init_ok;
void cypd_charger_init_complete(void)
{
	charger_init_ok = 1;
}
void cypd_update_power(void)
{
	if (!charger_init_ok) {
		system_power_present = 0;
		return;
	}

	if (extpower_is_present() ||
		battery_is_present() == BP_YES)
		system_power_present = 1;
	else
		system_power_present = 0;
}

int cypd_update_power_status(void)
{
	int i;
	int rv = EC_SUCCESS;
	int power_stat = 0;
	if (battery_is_present() == BP_YES) {
		power_stat |= BIT(3);
	}
	if (extpower_is_present()) {
		power_stat |= BIT(1) + BIT(2);
	}

	cypd_update_power();

	CPRINTS("%s power_stat 0x%x", __func__, power_stat);

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		rv = cypd_write_reg8_wait_ack(i, CYP5525_POWER_STAT, power_stat);
		if (rv != EC_SUCCESS)
			break;
	}
	return rv;
}

void enable_compliance_mode(int controller)
{
	int rv;
	uint32_t debug_register = 0xD000000;
	int debug_ctl = 0x0100;

	/* Write 0x0D000000 to address 0x0048 */
	rv = cypd_write_reg_block(controller, CYP5525_ICL_BB_RETIMER_DAT_REG,
			(void *) &debug_register, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_DAT_REG fail");

	/* Write 0x0100 to address 0x0046 */
	rv = cypd_write_reg16(controller, CYP5525_ICL_BB_RETIMER_CMD_REG, debug_ctl);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_CMD_REG fail");
}

void entry_tbt_mode(int controller)
{
	int rv;
	uint8_t force_tbt_mode = 0x01;

	rv = cypd_write_reg8(controller, CYP5525_ICL_CTRL_REG, force_tbt_mode);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_CTRL_REG fail");
}

void exit_tbt_mode(int controller)
{
	int rv;
	uint8_t force_tbt_mode = 0x00;

	rv = cypd_write_reg8(controller, CYP5525_ICL_CTRL_REG, force_tbt_mode);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_CTRL_REG fail");
}

int check_tbt_mode(int controller)
{
	int rv;
	int data;

	rv = cypd_read_reg8(controller, CYP5525_ICL_STS_REG, &data);
	if (rv != EC_SUCCESS)
		CPRINTS("Read CYP5525_ICL_STS_REG fail");

	return data;
}

void cypd_bb_retimer_cmd(uint8_t cmd)
{
       int i;

       for (i = 0; i < PD_CHIP_COUNT; i++) {
               cypd_write_reg16_wait_ack(i, CYP5225_USER_BB_POWER_EVT,  cmd);
       }
}


void cypd_reset_source_ports(void)
{
	int i;
	int port;
	int controller;

	CPRINTS("Resetting source ports");
	for (i = 0; i < PD_PORT_COUNT; i++) {
			port = i & 1;
			controller = i >>1;
			if (pd_port_states[i].power_role == PD_ROLE_SOURCE) {
					CPRINTS("reset port %d:%d", controller, port);
					cypd_write_reg8(controller, CYP5525_PD_CONTROL_REG(port), CYPD_PD_CMD_HARD_RESET);
			}
	}
}

enum power_state saved_bb_power_state = POWER_S0;

void cypd_set_retimer_power(enum power_state power)
{
	if (power != saved_bb_power_state) {
			saved_bb_power_state = power;
			cypd_enque_evt(CYPD_EVT_RETIMER_PWR, 0);
	}
}


void cypd_set_power_active(enum power_state power)
{
	cypd_enque_evt(CYPD_EVT_S_CHANGE, 0);
}

void update_system_power_state(void)
{

	enum power_state ps = power_get_state();
	switch (ps) {
	case POWER_G3:
	case POWER_S5:
	case POWER_S5G3:
	case POWER_S3S5:
		cypd_set_power_state(CYP5525_POWERSTATE_S5);
		break;
	default:
		cypd_set_power_state(CYP5525_POWERSTATE_S0);
		break;


	}

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
	#define CYPD_SETUP_CMDS_LEN  5
	struct {
		int reg;
		int value;
		int length;
		int status_reg;
	} const cypd_setup_cmds[] = {
		{ CYP5525_EVENT_MASK_REG(0), 0x7ffff, 4, CYP5525_PORT0_INTR},	/* Set the port 0 event mask */
		{ CYP5525_EVENT_MASK_REG(1), 0x7ffff, 4, CYP5525_PORT1_INTR },	/* Set the port 1 event mask */
		{ CYP5525_VDM_EC_CONTROL_REG(0), CYP5525_EXTEND_MSG_CTRL_EN, 1, CYP5525_PORT0_INTR},	/* Set the port 0 event mask */
		{ CYP5525_VDM_EC_CONTROL_REG(1), CYP5525_EXTEND_MSG_CTRL_EN, 1, CYP5525_PORT1_INTR },	/* Set the port 1 event mask */
		{ CYP5525_PD_CONTROL_REG(0), CYPD_PD_CMD_EC_INIT_COMPLETE, CYP5525_PORT0_INTR },	/* EC INIT Complete */
	};
	BUILD_ASSERT(ARRAY_SIZE(cypd_setup_cmds) == CYPD_SETUP_CMDS_LEN);

	/* Make sure the interrupt is not asserted before we start */
	if (gpio_get_level(pd_chip_config[controller].gpio) == 0) {
		rv = cypd_get_int(controller, &data);
		CPRINTS("%s int already pending 0x%04x", __func__, data);
		cypd_clear_int(controller, CYP5525_DEV_INTR+CYP5525_PORT0_INTR+CYP5525_PORT1_INTR+CYP5525_UCSI_INTR);
	}
	for (i = 0; i < CYPD_SETUP_CMDS_LEN; i++) {
		rv = cypd_write_reg_block(controller, cypd_setup_cmds[i].reg,
		(void *)&cypd_setup_cmds[i].value, cypd_setup_cmds[i].length);
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


void cypd_set_source_pdo_mask(int enabled_mask)
{
	int i;
	int port;
	int controller;

	for (i = 0; i < PD_PORT_COUNT; i++) {
		port = i % 2;
		controller = i >>1;
		cypd_write_reg8(controller, CYP5525_SELECT_SOURCE_PDO_REG(port), enabled_mask);
	}
}

/*
 * This enables setting up to 6 source PDOs
 * we only use 1 source PDO 5V3A.
 */
void cypd_set_source_pdo(int controller, int port, uint32_t *pdos, int num_pdos, bool unconstrained_power)
{
	uint32_t data[7] = {0};
	int i;
	int enabled_mask = 0;
	if (unconstrained_power) {
		enabled_mask |= BIT(7);
	}

	data[0] = 0x53524350; /* signature = SRCP */

	for (i = 0; i < MIN(6, num_pdos); i++) {
		data[i+1] = pdos[i];
		enabled_mask |= 1<<i;
	}

	cypd_write_reg_block(controller, CYP5525_WRITE_DATA_MEMORY_REG(port, 0),
								(void *)data, ARRAY_SIZE(data) * sizeof(uint32_t));
	cypd_write_reg8(controller, CYP5525_SELECT_SOURCE_PDO_REG(port), enabled_mask);

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
void cypd_send_msg(int controller, int port, uint32_t pd_header, uint16_t ext_hdr, bool pd30, bool response_timer, void *data, uint32_t data_size)
{
	uint16_t header[2] = {0};
	uint16_t dm_control_data;

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

	cypd_write_reg_block(controller, CYP5525_WRITE_DATA_MEMORY_REG(port, 0),
		(void *)header, 4);

	cypd_write_reg_block(controller, CYP5525_WRITE_DATA_MEMORY_REG(port, 4),
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
		dm_control_data |= CYP5525_DM_CTRL_EXTENDED_DATA_REQUEST;
	if (pd30)
		dm_control_data |= CYP5525_DM_CTRL_PD3_DATA_REQUEST;
	if (!response_timer)
		dm_control_data |= CYP5525_DM_CTRL_SENDER_RESPONSE_TIMER_DISABLE;
	dm_control_data += ((data_size + 4) << 8);

	cypd_write_reg16(controller, CYP5525_DM_CONTROL_REG(port), dm_control_data);
}

void cypd_response_get_battery_capability(int controller, int port,
	uint32_t pd_header, enum pd_msg_type sop_type)
{
	int port_idx = (controller << 1) + port;
	int ext_header = 0;
	bool chunked = PD_EXT_HEADER_CHUNKED(rx_emsg[port_idx].header);
	uint16_t msg[5] = {0, 0, 0, 0, 0};
	uint32_t header = PD_EXT_BATTERY_CAP + PD_HEADER_SOP(sop_type);

	ext_header = 9;
	/* Set extended header */
	if (chunked) {
		ext_header |= BIT(15);
	}
	/* Set VID */
	msg[0] = VENDOR_ID;

	/* Set PID */
	msg[1] = PRODUCT_ID;

	if (battery_is_present() == BP_YES) {
		/*
		 * We only have one fixed battery,
		 * so make sure batt cap ref is 0.
		 */
		if (rx_emsg[port_idx].buf[0] != 0) {
			/* Invalid battery reference */
			msg[4] = 1;
		} else {
			uint32_t v;
			uint32_t c;

			/*
			 * The Battery Design Capacity field shall return the
			 * Battery’s design capacity in tenths of Wh. If the
			 * Battery is Hot Swappable and is not present, the
			 * Battery Design Capacity field shall be set to 0. If
			 * the Battery is unable to report its Design Capacity,
			 * it shall return 0xFFFF
			 */
			msg[2] = 0xffff;

			/*
			 * The Battery Last Full Charge Capacity field shall
			 * return the Battery’s last full charge capacity in
			 * tenths of Wh. If the Battery is Hot Swappable and
			 * is not present, the Battery Last Full Charge Capacity
			 * field shall be set to 0. If the Battery is unable to
			 * report its Design Capacity, the Battery Last Full
			 * Charge Capacity field shall be set to 0xFFFF.
			 */
			msg[3] = 0xffff;

			if (battery_design_voltage(&v) == 0) {
				if (battery_design_capacity(&c) == 0) {
					/*
					 * Wh = (c * v) / 1000000
					 * 10th of a Wh = Wh * 10
					 */
					msg[2] = DIV_ROUND_NEAREST((c * v),
								100000);
				}

				if (battery_full_charge_capacity(&c) == 0) {
					/*
					 * Wh = (c * v) / 1000000
					 * 10th of a Wh = Wh * 10
					 */
					msg[3] = DIV_ROUND_NEAREST((c * v),
								100000);
				}
			}
		}
	}
	cypd_send_msg(controller, port, header, ext_header,  false, false, (void *)msg, ARRAY_SIZE(msg)*sizeof(uint16_t));

}

int cypd_response_get_battery_status(int controller, int port, uint32_t pd_header, enum pd_msg_type sop_type)
{
	int rv = 0;
	uint32_t msg = 0;
	uint32_t header = PD_DATA_BATTERY_STATUS + PD_HEADER_SOP(sop_type);
	int port_idx = (controller << 1) + port;

	if (battery_is_present() == BP_YES) {
		/*
		 * We only have one fixed battery,
		 * so make sure batt cap ref is 0.
		 */
		if (rx_emsg[port_idx].buf[0] != 0) {
			/* Invalid battery reference */
			msg |= BSDO_INVALID;
		} else {
			uint32_t v;
			uint32_t c;

			if (battery_design_voltage(&v) != 0 ||
					battery_remaining_capacity(&c) != 0) {
				msg |= BSDO_CAP(BSDO_CAP_UNKNOWN);
			} else {
				/*
				 * Wh = (c * v) / 1000000
				 * 10th of a Wh = Wh * 10
				 */
				msg |= BSDO_CAP(DIV_ROUND_NEAREST((c * v),
								100000));
			}

			/* Battery is present */
			msg |= BSDO_PRESENT;

			/*
			 * For drivers that are not smart battery compliant,
			 * battery_status() returns EC_ERROR_UNIMPLEMENTED and
			 * the battery is assumed to be idle.
			 */
			if (battery_status(&c) != 0) {
				msg |= BSDO_IDLE; /* assume idle */
			} else {
				if (c & STATUS_FULLY_CHARGED)
					/* Fully charged */
					msg |= BSDO_IDLE;
				else if (c & STATUS_DISCHARGING)
					/* Discharging */
					msg |= BSDO_DISCHARGING;
				/* else battery is charging.*/
			}
		}
	} else {
		msg = BSDO_CAP(BSDO_CAP_UNKNOWN);
	}

	cypd_send_msg(controller, port, header, 0,  true, false, &msg, 4);

	return rv;
}

int cypd_handle_extend_msg(int controller, int port, int len, enum pd_msg_type sop_type)
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
		CYP5525_READ_DATA_MEMORY_REG(port, 0), (void *)&(rx_emsg[port_idx].len), len);
		/*avoid a memcopy so direct copy into the buffer and then swap header and len
		 * look at the memory layout for the rx_emsg structure to see why we do this */
	rx_emsg[port_idx].header = rx_emsg[port_idx].len >> 16;
	pd_header = (rx_emsg[port_idx].len & 0xFFFF) + PD_HEADER_SOP(sop_type);
	rx_emsg[port_idx].len = len-4;

	/* Extended field shall be set to 1*/
	if (!PD_HEADER_EXT(pd_header))
		return EC_ERROR_INVAL;

	type = PD_HEADER_TYPE(pd_header);

	switch (type) {
	case PD_EXT_GET_BATTERY_CAP:
		cypd_response_get_battery_capability(controller, port, pd_header, sop_type);
		break;
	case PD_EXT_GET_BATTERY_STATUS:
		rv = cypd_response_get_battery_status(controller, port, pd_header, sop_type);
		break;
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

void cypd_update_port_state(int controller, int port)
{
	int rv;
	uint8_t pd_status_reg[4];
	uint8_t pdo_reg[4];
	uint8_t rdo_reg[4];

	int typec_status_reg;
	int pd_current = 0;
	int pd_voltage = 0;
	int rdo_max_current = 0;
	int type_c_current = 0;
	int port_idx = (controller << 1) + port;

	rv = cypd_read_reg_block(controller, CYP5525_PD_STATUS_REG(port), pd_status_reg, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_PD_STATUS_REG failed");
	pd_port_states[port_idx].pd_state = pd_status_reg[1] & BIT(2) ? 1 : 0; /*do we have a valid PD contract*/
	pd_port_states[port_idx].power_role = pd_status_reg[1] & BIT(0) ? PD_ROLE_SOURCE : PD_ROLE_SINK;
	pd_port_states[port_idx].data_role = pd_status_reg[0] & BIT(6) ? PD_ROLE_DFP : PD_ROLE_UFP;
	pd_port_states[port_idx].vconn =  pd_status_reg[1] & BIT(5) ? PD_ROLE_VCONN_SRC : PD_ROLE_VCONN_OFF;

	rv = cypd_read_reg8(controller, CYP5525_TYPE_C_STATUS_REG(port), &typec_status_reg);
	if (rv != EC_SUCCESS)
		CPRINTS("CYP5525_TYPE_C_STATUS_REG failed");

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

	rv = cypd_read_reg_block(controller, CYP5525_CURRENT_PDO_REG(port), pdo_reg, 4);
	pd_current = (pdo_reg[0] + ((pdo_reg[1] & 0x3) << 8)) * 10;
	pd_voltage = (((pdo_reg[1] & 0xFC) >> 2) + ((pdo_reg[2] & 0xF) << 6)) * 50;

	cypd_read_reg_block(controller, CYP5525_CURRENT_RDO_REG(port), rdo_reg, 4);
	/*rdo_current = ((rdo_reg[0] + (rdo_reg[1]<<8)) & 0x3FF)*10,*/
	rdo_max_current = (((rdo_reg[1]>>2) + (rdo_reg[2]<<6)) & 0x3FF)*10;

	/*
	 * The port can have several states active:
	 * 1. Type C active (with no PD contract) CC resistor negociation only
	 * 2. Type C active with PD contract
	 * 3. Not active
	 * Each of 1 and 2 can be either source or sink
	 * */

	if (pd_port_states[port_idx].c_state == CYPD_STATUS_SOURCE) {
		typec_set_input_current_limit(port_idx, type_c_current, TYPE_C_VOLTAGE);
		charge_manager_set_ceil(port_idx, CEIL_REQUESTOR_PD,
							type_c_current);
	} else {
		typec_set_input_current_limit(port_idx, 0, 0);
		charge_manager_set_ceil(port,
			CEIL_REQUESTOR_PD,
			CHARGE_CEIL_NONE);
	}
	if (pd_port_states[port_idx].c_state == CYPD_STATUS_SINK) {
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


	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		charge_manager_update_dualrole(port_idx, CAP_DEDICATED);
	}
}
void cypd_print_version(int controller, const char *vtype, uint8_t *data)
{
		CPRINTS("Controller %d  %s version B:%d.%d.%d.%d", /* AP:%d.%d.%d.%c%c */
		controller, vtype,
		(data[3]>>4) & 0xF, (data[3]) & 0xF, data[2], data[0] + (data[1]<<8)
		/* (data[7]>>4) & 0xF, (data[7]) & 0xF, data[6], data[5], data[4] */
		);
}
void cyp5525_get_version(int controller)
{
	int rv;
	uint8_t data[24];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;

	rv = i2c_read_offset16_block(i2c_port, addr_flags, CYP5525_READ_ALL_VERSION_REG, data, 24);
	if (rv != EC_SUCCESS)
		CPRINTS("READ_ALL_VERSION_REG failed");
	/*cypd_print_version(controller, "Boot", data);*/
	cypd_print_version(controller, "App1", data+8);
	cypd_print_version(controller, "App2", data+16);
}


void cyp5525_port_int(int controller, int port)
{
	int i, rv, response_len;
	uint8_t data2[32];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;
	int port_idx = (controller << 1) + port;
	enum pd_msg_type sop_type;
	rv = i2c_read_offset16_block(i2c_port, addr_flags, CYP5525_PORT_PD_RESPONSE_REG(port), data2, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("PORT_PD_RESPONSE_REG failed");
	print_pd_response_code(controller,
		port,
		data2[0],
		data2[1]);

	response_len = data2[1];
	switch (data2[0]) {
	case CYPD_RESPONSE_PORT_DISCONNECT:
		CPRINTS("CYPD_RESPONSE_PORT_DISCONNECT");
		pd_port_states[port_idx].current = 0;
		pd_port_states[port_idx].voltage = 0;
		pd_set_input_current_limit(port_idx, 0, 0);
		cypd_update_port_state(controller, port);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			charge_manager_update_dualrole(port_idx, CAP_UNKNOWN);
		break;
	case CYPD_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE:
		CPRINTS("CYPD_RESPONSE_PD_CONTRACT_NEGOTIATION_COMPLETE %d", port_idx);
		/*todo we can probably clean this up to remove some of this*/
		cypd_update_port_state(controller, port);
		break;
	case CYPD_RESPONSE_PORT_CONNECT:
		CPRINTS("CYPD_RESPONSE_PORT_CONNECT %d", port_idx);
		cypd_update_port_state(controller, port);
		break;
	case CYPD_RESPONSE_EXT_MSG_SOP_RX:
	case CYPD_RESPONSE_EXT_SOP1_RX:
	case CYPD_RESPONSE_EXT_SOP2_RX:
		if (data2[0] == CYPD_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = PD_MSG_SOP;
		else if (data2[0] == CYPD_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = PD_MSG_SOP_PRIME;
		else if (data2[0] == CYPD_RESPONSE_EXT_MSG_SOP_RX)
			sop_type = PD_MSG_SOP_PRIME_PRIME;
		cypd_handle_extend_msg(controller, port, response_len, sop_type);
		CPRINTS("CYP_RESPONSE_RX_EXT_MSG");
		break;
	default:
		if (response_len && verbose_msg_logging) {
			CPRINTF("Port:%d Data:0x", port_idx);
			i2c_read_offset16_block(i2c_port, addr_flags, CYP5525_READ_DATA_MEMORY_REG(port, 0), data2, MIN(response_len, 32));
			for (i = 0; i < response_len; i++) {
				CPRINTF("%02x", data2[i]);
			}
			CPRINTF("\n");
		}
		break;
	}
}

void pd_bb_powerdown_deferred(void)
{
	pending_retimer_init(0);
	if (!keep_pch_power() && power_get_state() == POWER_G3) {
		CPRINTS("BB PCH PWR down");
		gpio_set_level(GPIO_PCH_PWR_EN, 0);
	}
}
DECLARE_DEFERRED(pd_bb_powerdown_deferred);

int cyp5525_device_int(int controller)
{
	int data;

	if (cypd_read_reg16(controller, CYP5525_RESPONSE_REG, &data) == EC_SUCCESS) {
		print_pd_response_code(controller,
		-1,
		data & 0xff,
		data>>8);
		switch (data & 0xFF) {
		case CYPD_RESPONSE_RESET_COMPLETE:
					CPRINTS("PD%d Reset Complete", controller);

			pd_chip_config[controller].state = CYP5525_STATE_POWER_ON;
			/* Run state handler to set up controller */
			cypd_enque_evt(4<<controller, 0);
			break;
		default:
			CPRINTS("INTR_REG CTRL:%d TODO Device 0x%x", controller, data & 0xFF);
		}
	} else {
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}

void cypd_handle_state(int controller)
{
	int data;
	//int i;
	int delay = 0;

	switch (pd_chip_config[controller].state) {
	case CYP5525_STATE_POWER_ON:
		/*poll to see if the controller has booted yet*/
		if (cypd_read_reg8(controller, CYP5525_DEVICE_MODE, &data) == EC_SUCCESS) {
			if ((data & 0x03) == 0x00) {
				/*pd_chip_config[controller].state = CYP5525_STATE_BOOTLOADER;*/
				CPRINTS("CYPD %d is in bootloader 0x%04x", controller, data);
				delay = 25*MSEC;
				if (cypd_read_reg16(controller, CYP5525_BOOT_MODE_REASON, &data)
						== EC_SUCCESS) {
					CPRINTS("CYPD bootloader reason 0x%02x", data);
				}

			} else {
				pd_chip_config[controller].state = CYP5525_STATE_APP_SETUP;
			}
		}
		/*try again in a while*/
		cypd_enque_evt(4<<controller, delay);
		break;

	case CYP5525_STATE_APP_SETUP:
			gpio_disable_interrupt(pd_chip_config[controller].gpio);
			cyp5525_get_version(controller);
			cypd_write_reg8_wait_ack(controller, CYP5225_USER_MAINBOARD_VERSION, board_get_version());

			/*for(i=0; i < 50;i++) {
				if (gpio_get_level(GPIO_PWR_3V5V_PG) && 
						((charger_current_battery_params()->is_present == BP_YES && 
							!(charger_current_battery_params()->flags & BATT_FLAG_BAD_ANY))
						)
					)
					break;
				usleep(MSEC);
				
			}*/
			pending_retimer_init(1);
			cypd_update_power();
			if (system_power_present) {
				gpio_set_level(GPIO_PCH_PWR_EN, 1);
				CPRINTS("PD PCH ON");
			} else {
				CPRINTS("Dead Battery Condition?");
			}

			hook_call_deferred(&pd_bb_powerdown_deferred_data, BB_PWR_DOWN_TIMEOUT);
			cypd_update_power_status();

			cypd_set_power_state(CYP5525_POWERSTATE_S5);
			

			cyp5525_setup(controller);
			cypd_update_port_state(controller, 0);
			cypd_update_port_state(controller, 1);

			cyp5525_ucsi_startup(controller);
			gpio_enable_interrupt(pd_chip_config[controller].gpio);
			cypd_write_reg16_wait_ack(controller, CYP5225_USER_BB_POWER_EVT,  RT_EVT_VSYS_ADDED);
			update_system_power_state();

			//if (power_get_state() == POWER_S0 || 
			//	power_get_state() == POWER_S0ix) {
				/*Handle unexpected PD controller resets when we are powered on*/
			//	cypd_enque_evt(CYPD_EVT_RETIMER_PWR, 0);
			//}

			CPRINTS("CYPD %d Ready!", controller);
			pd_chip_config[controller].state = CYP5525_STATE_READY;
		break;
	default:
		CPRINTS("PD handle_state but in 0x%02x state!", pd_chip_config[controller].state);
		break;
	}

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
	/* Process device interrupt*/
	if (data & CYP5525_DEV_INTR) {
		cyp5525_device_int(controller);
		clear_mask |= CYP5525_DEV_INTR;
	}
	/*CPRINTS("INTR_REG read value: 0x%02x", data);*/
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
	if (data & CYP5525_ICLR_INTR) {
		clear_mask |= CYP5525_ICLR_INTR;
	}

	if (data & CYP5525_UCSI_INTR) {
		/* CPRINTS("P%d read ucsi data!", controller); */
		ucsi_read_tunnel(controller);
		cypd_clear_int(controller, CYP5525_UCSI_INTR);
	}
	cypd_clear_int(controller, clear_mask);
}



static uint8_t cypd_int_task_id;

void cypd_enque_evt(int evt, int delay)
{
	task_set_event(TASK_ID_CYPD, evt, delay);
}


void pd0_chip_interrupt_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CYPD_EVT_INT_CTRL_0, 0);

}
DECLARE_DEFERRED(pd0_chip_interrupt_deferred);
void pd0_chip_interrupt(enum gpio_signal signal)
{
	/* GPIO_PCH_PWR_EN must be first */
	if (system_power_present) {
		gpio_set_level(GPIO_PCH_PWR_EN, 1);
		hook_call_deferred(&pd0_chip_interrupt_deferred_data, 0);
	}
	//task_set_event(TASK_ID_CYPD, CYPD_EVT_INT_CTRL_0, 0);
	pending_retimer_init(1);

	hook_call_deferred(&pd_bb_powerdown_deferred_data, BB_PWR_DOWN_TIMEOUT);
}

void pd1_chip_interrupt_deferred(void)
{
	task_set_event(TASK_ID_CYPD, CYPD_EVT_INT_CTRL_1, 0);

}
DECLARE_DEFERRED(pd1_chip_interrupt_deferred);
void pd1_chip_interrupt(enum gpio_signal signal)
{
	/* GPIO_PCH_PWR_EN must be first */
	if (system_power_present) {
		gpio_set_level(GPIO_PCH_PWR_EN, 1);
		hook_call_deferred(&pd0_chip_interrupt_deferred_data, 0);
	}
	//task_set_event(TASK_ID_CYPD, CYPD_EVT_INT_CTRL_1, 0);
	pending_retimer_init(1);

	hook_call_deferred(&pd_bb_powerdown_deferred_data, BB_PWR_DOWN_TIMEOUT);
}
/*
void soc_plt_reset_interrupt_deferred(void)
{
	CPRINTS("PLT_RESET IS NOW %d", gpio_get_level(GPIO_PLT_RST_L));
	if (gpio_get_level(GPIO_PLT_RST_L) == 1)
		cypd_enque_evt(CYPD_PROCESS_PLT_RESET, 0);
}
DECLARE_DEFERRED(soc_plt_reset_interrupt_deferred);
*/
void soc_plt_reset_interrupt(enum gpio_signal signal)
{
	/*Delay is to allow BB retimer to boot before configuration*/
	/*hook_call_deferred(&soc_plt_reset_interrupt_deferred_data, 25 * MSEC);*/
}

/* Called on AP S5 -> S3 transition */
/*
static void pd_enter_s3(void)
{
	cypd_enque_evt(CYPD_EVT_S_CHANGE, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP,
		pd_enter_s3,
		HOOK_PRIO_DEFAULT);
*/

/* Called on AP S3 -> S5 transition */
/*
static void pd_enter_s5(void)
{
	cypd_enque_evt(CYPD_EVT_S_CHANGE, 0);

}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN,
		pd_enter_s5,
		HOOK_PRIO_DEFAULT);
*/
/* Called on AP S3 -> S0 transition */
/*
static void pd_enter_s0(void)
{
	cypd_enque_evt(CYPD_EVT_S_CHANGE, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pd_enter_s0,
	     HOOK_PRIO_DEFAULT);
*/

void cypd_reinitialize(void)
{
	int i;

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		pd_chip_config[i].state = CYP5525_STATE_POWER_ON;
		/* Run state handler to set up controller */
		cypd_enque_evt(4<<i, 0);
	}
}

void cypd_interrupt_handler_task(void *p)
{
	int i, j, evt;
	cypd_int_task_id = task_get_current();

	/* Initialize all charge suppliers to 0 */
	for (i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}
	/* trigger the handle_state to start setup in task */
	cypd_enque_evt(CYPD_EVT_STATE_CTRL_0 | CYPD_EVT_STATE_CTRL_1, 0);

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		gpio_enable_interrupt(pd_chip_config[i].gpio);
		cypd_enque_evt(CYPD_EVT_STATE_CTRL_0<<i, 0);
	}
	while (1) {
		evt = task_wait_event(10*MSEC);

		if (firmware_update)
			continue;

		if (evt & CYPD_EVT_AC_PRESENT) {
			CPRINTS("GPIO_AC_PRESENT_PD_L changed: value: 0x%02x", gpio_get_level(GPIO_AC_PRESENT_PD_L));
		}

		if (evt & CYPD_EVT_PLT_RESET) {
			CPRINTS("PD Event Platform Reset!");
		}

		if (evt & CYPD_EVT_RETIMER_PWR) {
			if (saved_bb_power_state == POWER_G3S5) {
					cypd_bb_retimer_cmd(RT_EVT_VSYS_ADDED);
			}
			if (saved_bb_power_state == POWER_G3) {
					cypd_bb_retimer_cmd(RT_EVT_VSYS_REMOVED);
			}
		}

		if (evt & CYPD_EVT_S_CHANGE) {
			update_system_power_state();
		}

		if (evt & CYPD_EVT_INT_CTRL_0) {
			cyp5525_interrupt(0);
		}
		if (evt & CYPD_EVT_INT_CTRL_1) {
			cyp5525_interrupt(1);
		}
		if (evt & CYPD_EVT_STATE_CTRL_0) {
			cypd_handle_state(0);
			task_wait_event_mask(TASK_EVENT_TIMER,10);
		}
		if (evt & CYPD_EVT_STATE_CTRL_1) {
			cypd_handle_state(1);
			task_wait_event_mask(TASK_EVENT_TIMER,10);
		}
		if (evt & CYPD_EVT_UPDATE_PWRSTAT) {
			cypd_update_power_status();
		}

		if (evt & (CYPD_EVT_INT_CTRL_0 | CYPD_EVT_INT_CTRL_1 |
					CYPD_EVT_STATE_CTRL_0 | CYPD_EVT_STATE_CTRL_1)) {
			/*If we just processed an event or sent some commands
				* wait a bit for the pd controller to clear any pending
				* interrupt requests*/
			usleep(50);
		}

		check_ucsi_event_from_host();

		for (i = 0; i < PD_CHIP_COUNT; i++) {
			if (gpio_get_level(pd_chip_config[i].gpio) == 0) {
				cypd_enque_evt(1<<i, 0);
			}
		}

	}
}

int cypd_get_pps_power_budget(void)
{
	/* TODO:
	 * Implement PPS function and get pps power budget
	 */
	int power = 0;

	return power;
}


/* Stub out the following for charge manager, we dont use this for the bios */
void pd_send_host_event(int mask) { }

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

enum pd_power_role pd_get_power_role(int port)
{
	return pd_port_states[port].power_role;
}

int pd_is_connected(int port)
{
	return pd_port_states[port].c_state != CYPD_STATUS_NOTHING;
}

void pd_request_power_swap(int port)
{
	CPRINTS("TODO Implement %s port %d", __func__, port);
}

void pd_set_new_power_request(int port)
{
	/*we probably dont need to do this since we will always request max*/
	CPRINTS("TODO Implement %s port %d", __func__, port);
}

int pd_port_configuration_change(int port, enum pd_port_role port_role)
{
	/**
	 * Specification 5.3.3 Port Configuration Change descripes the steps to change
	 * port configuration.
	 *
	 * Step1: Disabled the port using PDPORT_ENABLE register
	 * Step2: Writed the data memory register in the follow format
	 *			Byte 0 : desired port role (0: Sink, 1: Source, 2: Dual Role)
	 *			Byte 1 : Default port role in case of Dual Role (0: Sink, 1: Source)
	 *			Byte 2 : DRP toggle enable (in case of Dual Role port)
	 *			Byte 3 : Try.SRC enable (in case of Daul Role port)
	 * Step3: Using the "Change PD Port Parameters" command in PD_CONTROL register
	 * Step4: Enabled the port using PDPORT_ENABLE register
	 */

	int controller = (port & 0x02);
	int cyp_port = (port & 0x01);
	int rv;
	uint8_t data[4] = {0};

	CPRINTS("Change port %d role.", port);

	data[0] = port_role;

	if (port_role == PORT_DUALROLE) {
		data[1] = PORT_SINK;	/* default port role = sink */
		data[2] = 0x01;			/* enable DRP toggle */
		data[3] = 0x01;			/* enable Try.SRC */
	}

	rv = cypd_write_reg8(controller, CYP5525_PDPORT_ENABLE_REG, (0x03 & ~BIT(cyp_port)));
	if (rv != EC_SUCCESS)
		return rv;

	/**
	 * Stopping an active PD port can take a long time (~1 second) in case VBUS is
	 * being provided and needs to be discharged.
	 */
	cyp5225_wait_for_ack(controller, 1 * SECOND);

	rv = cypd_write_reg_block(controller, CYP5525_WRITE_DATA_MEMORY_REG(port, 0), data, 4);
	if (rv != EC_SUCCESS)
		return rv;

	cyp5225_wait_for_ack(controller, 5000);


	rv = cypd_write_reg8(controller, CYP5525_PD_CONTROL_REG(cyp_port),
		CYPD_PD_CMD_CHANGE_PD_PORT_PARAMS);
	if (rv != EC_SUCCESS)
		return rv;

	cyp5225_wait_for_ack(controller, 5000);


	rv = cypd_write_reg8(controller, CYP5525_PDPORT_ENABLE_REG, 0x03);
	if (rv != EC_SUCCESS)
		return rv;

	return rv;
}

static void update_power_limit_deferred(void)
{
	update_soc_power_limit(false, false);
}
DECLARE_DEFERRED(update_power_limit_deferred);

/**
 * Set active charge port -- only one port can be active at a time.
 *
 * @param charge_port   Charge port to enable.
 *
 * Returns EC_SUCCESS if charge port is accepted and made active,
 * EC_ERROR_* otherwise.
 */
static int prev_charge_port = -1;
int board_set_active_charge_port(int charge_port)
{

	int mask;
	int i;
	int disable_lockout;

	if (prev_charge_port != -1 && prev_charge_port != charge_port) {
		update_soc_power_limit(false, true);
		gpio_set_level(GPIO_TYPEC0_VBUS_ON_EC, 0);
		gpio_set_level(GPIO_TYPEC1_VBUS_ON_EC, 0);
		gpio_set_level(GPIO_TYPEC2_VBUS_ON_EC, 0);
		gpio_set_level(GPIO_TYPEC3_VBUS_ON_EC, 0);
		usleep(250*MSEC);
	}
	prev_charge_port = charge_port;
	if (charge_port >=0){
		disable_lockout = 1;
		mask = 1 << charge_port;
		gpio_set_level(GPIO_TYPEC0_VBUS_ON_EC, mask & BIT(0) ? 1: 0);
		gpio_set_level(GPIO_TYPEC1_VBUS_ON_EC, mask & BIT(1) ? 1: 0);
		gpio_set_level(GPIO_TYPEC2_VBUS_ON_EC, mask & BIT(2) ? 1: 0);
		gpio_set_level(GPIO_TYPEC3_VBUS_ON_EC, mask & BIT(3) ? 1: 0);
	} else {
		disable_lockout = 0;
		gpio_set_level(GPIO_TYPEC0_VBUS_ON_EC, 1);
		gpio_set_level(GPIO_TYPEC1_VBUS_ON_EC, 1);
		gpio_set_level(GPIO_TYPEC2_VBUS_ON_EC, 1);
		gpio_set_level(GPIO_TYPEC3_VBUS_ON_EC, 1);
	}
	for (i = 0; i < PD_CHIP_COUNT; i++)
		cypd_write_reg8(i, CYP5225_USER_DISABLE_LOCKOUT, disable_lockout);

	cypd_enque_evt(CYPD_EVT_UPDATE_PWRSTAT, 100);
	hook_call_deferred(&update_power_limit_deferred_data, 100 * MSEC);
	CPRINTS("Updating %s port %d", __func__, charge_port);

	return EC_SUCCESS;
}

/**
 * Set the charge limit based upon desired maximum.
 *
 * @param port          Port number.
 * @param supplier      Charge supplier type.
 * @param charge_ma     Desired charge limit (mA).
 * @param charge_mv     Negotiated charge voltage (mV).
 */
void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	int prochot_ma;

	if (charge_ma < CONFIG_CHARGER_INPUT_CURRENT) {
		charge_ma = CONFIG_CHARGER_INPUT_CURRENT;
	}
	/* ac prochot should bigger than input current 
	 * And needs to be at least 128mA bigger than the adapter current*/
	prochot_ma = (DIV_ROUND_UP(charge_ma, 128) * 128);
	charge_ma = charge_ma * 95 / 100;

	if ((prochot_ma - charge_ma) < 128){
		charge_ma = prochot_ma - 128;
	}

	charge_set_input_current_limit(charge_ma, charge_mv);
	/* sync-up ac prochot with current change */
	isl9241_set_ac_prochot(0, prochot_ma);
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
	if (verbose_msg_logging) {
		CPRINTS("PD Controller %d Port %d  Code 0x%02x %s %s Len: 0x%02x",
		controller,
		port,
		id,
		code,
		id & 0x80 ? "Response" : "Event",
		len);
	}
}


void cypd_print_buff(const char *msg, void *buff, int len)
{
	int i;
	uint8_t *data = (uint8_t *)buff;

	CPRINTF("%s 0x", msg);
	for (i = len-1; i >=0; i--) {
		CPRINTF("%02x", data[i]);
	}
	CPRINTF("\n");
}

static int cmd_cypd_get_status(int argc, char **argv)
{
	int i, p, data;
	uint8_t data16[16];
	char *e;

	static const char * const mode[] = {"Boot", "FW1", "FW2", "Invald"};
	static const char * const port_status[] = {"Nothing", "Sink", "Source", "Debug", "Audio", "Powered Acc", "Unsupported", "Invalid"};
	static const char * const current_level[] = {"DefaultA", "1.5A", "3A", "InvA"};
	static const char * const state[] = {"ERR", "POWER_ON", "APP_SETUP", "READY", "BOOTLOADER"};
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
			CPRINTS("State: %s", state[pd_chip_config[i].state]);
			cypd_read_reg16(i, CYP5525_SILICON_ID, &data);
			CPRINTS("CYPD_SILICON_ID: 0x%04x", data);
			cyp5525_get_version(i);
			cypd_read_reg8(i, CYP5525_DEVICE_MODE, &data);
			CPRINTS("CYPD_DEVICE_MODE: 0x%02x %s", data, mode[data & 0x03]);
			cypd_read_reg_block(i, CYP5525_HPI_VERSION, data16, 4);
			CPRINTS("HPI_VERSION: 0x%02x%02x%02x%02x", data16[3], data16[2], data16[1], data16[0]);
			cypd_read_reg8(i, CYP5525_INTR_REG, &data);
			CPRINTS("CYPD_INTR_REG: 0x%02x %s %s %s %s",
						data,
						data & CYP5525_DEV_INTR ? "DEV" : "",
						data & CYP5525_PORT0_INTR ? "PORT0" : "",
						data & CYP5525_PORT1_INTR ? "PORT1" : "",
						data & CYP5525_UCSI_INTR ? "UCSI" : "");

			cypd_read_reg16(i, CYP5525_RESPONSE_REG, &data);
			CPRINTS("CYPD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg16(i, CYP5525_PORT_PD_RESPONSE_REG(0), &data);
			CPRINTS("CYPD_PORT0_PD_RESPONSE_REG: 0x%02x", data);
			cypd_read_reg16(i, CYP5525_PORT_PD_RESPONSE_REG(1), &data);
			CPRINTS("CYPD_PORT1_PD_RESPONSE_REG: 0x%02x", data);


			cypd_read_reg8(i, CYP5525_BOOT_MODE_REASON, &data);
			CPRINTS("CYPD_BOOT_MODE_REASON: 0x%02x", data);

			cypd_read_reg8(i, CYP5525_PDPORT_ENABLE_REG, &data);
			CPRINTS("CYPD_PDPORT_ENABLE_REG: 0x%04x", data);

			cypd_read_reg8(i, CYP5525_POWER_STAT, &data);
			CPRINTS("CYPD_POWER_STAT: 0x%02x", data);

			cypd_read_reg8(i, CYP5525_ICL_STS_REG, &data);
			CPRINTS("CYP5525_ICL_STS_REG: 0x%04x", data);

			cypd_read_reg8(i, CYP5525_SYS_PWR_STATE, &data);
			CPRINTS("CYPD_SYS_PWR_STATE: 0x%02x", data);
			for (p = 0; p < 2; p++) {
				CPRINTS("=====Port %d======", p);
				cypd_read_reg_block(i, CYP5525_PD_STATUS_REG(p), data16, 4);
				CPRINTS("PD_STATUS %s DataRole:%s PowerRole:%s Vconn:%s",
						data16[1] & BIT(2) ? "Contract" : "NoContract",
						data16[0] & BIT(6) ? "DFP" : "UFP",
						data16[1] & BIT(0) ? "Source" : "Sink",
						data16[1] & BIT(5) ? "En" : "Dis");
				cypd_read_reg8(i, CYP5525_TYPE_C_STATUS_REG(p), &data);
				CPRINTS("   TYPE_C_STATUS : %s %s %s %s %s",
							data & 0x1 ? "Connected" : "Not Connected",
							data & 0x2 ? "CC2" : "CC1",
							port_status[(data >> 2) & 0x7],
							data & 0x20 ? "Ra" : "NoRa",
							current_level[(data >> 6) & 0x03]);
				cypd_read_reg_block(i, CYP5525_CURRENT_RDO_REG(p), data16, 4);
				CPRINTS("             RDO : Current:%dmA MaxCurrent%dmA 0x%08x",
						((data16[0] + (data16[1]<<8)) & 0x3FF)*10,
						(((data16[1]>>2) + (data16[2]<<6)) & 0x3FF)*10,
						*(uint32_t *)data16);

				cypd_read_reg_block(i, CYP5525_CURRENT_PDO_REG(p), data16, 4);
				CPRINTS("             PDO : MaxCurrent:%dmA Voltage%dmA 0x%08x",
						((data16[0] + (data16[1]<<8)) & 0x3FF)*10,
						(((data16[1]>>2) + (data16[2]<<6)) & 0x3FF)*50,
						*(uint32_t *)data16);
				cypd_read_reg8(i, CYP5525_TYPE_C_VOLTAGE_REG(p), &data);
				CPRINTS("  TYPE_C_VOLTAGE : %dmV", data*100);
				cypd_read_reg16(i, CYP5525_PORT_INTR_STATUS_REG(p), &data);
				CPRINTS(" INTR_STATUS_REG0: 0x%02x", data);
				cypd_read_reg16(i, CYP5525_PORT_INTR_STATUS_REG(p)+2, &data);
				CPRINTS(" INTR_STATUS_REG1: 0x%02x", data);
				/* Flush console to avoid truncating output */
				cflush();
			}
			CPRINTS("=====UCSI======");
			cypd_read_reg16(i, CYP5525_VERSION_REG, &data);
			CPRINTS(" Version: 0x%02x", data);
			cypd_read_reg_block(i, CYP5525_CCI_REG, data16, 4);
			cypd_print_buff("     CCI:", data16, 4);
			cypd_read_reg_block(i, CYP5525_CONTROL_REG, data16, 8);
			cypd_print_buff(" Control:", data16, 8);
			cypd_read_reg_block(i, CYP5525_MESSAGE_IN_REG, data16, 16);
			cypd_print_buff(" Msg  In:", data16, 16);
			cypd_read_reg_block(i, CYP5525_MESSAGE_OUT_REG, data16, 16);
			cypd_print_buff(" Msg Out:", data16, 16);

		}

	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cypdstatus, cmd_cypd_get_status,
			"[number]",
			"Get Cypress PD controller status");


static int cmd_cypd_control(int argc, char **argv)
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
				gpio_enable_interrupt(pd_chip_config[i].gpio);
			else
				gpio_disable_interrupt(pd_chip_config[i].gpio);
		} else if (!strncmp(argv[1], "reset", 5)) {
			cypd_write_reg8(i, CYP5525_PDPORT_ENABLE_REG, 0);
			/*can take up to 650ms to discharge port for disable*/
			cyp5225_wait_for_ack(i, 65000);
			cypd_clear_int(i, CYP5525_DEV_INTR +
							CYP5525_PORT0_INTR +
							CYP5525_PORT1_INTR +
							CYP5525_UCSI_INTR);
			usleep(50);
			CPRINTS("Full reset PD controller %d", i);
			/*
			 * see if we can talk to the PD chip yet - issue a reset command
			 * Note that we cannot issue a full reset command if the PD controller
			 * has a device attached - as it will return with an invalid command
			 * due to needing to disable all ports first.
			 */
			if (cyp5525_reset(i) == EC_SUCCESS) {
				CPRINTS("reset ok %d", i);
			}
		} else if (!strncmp(argv[1], "clearint", 8)) {
			cypd_clear_int(i, CYP5525_DEV_INTR +
							CYP5525_PORT0_INTR +
							CYP5525_PORT1_INTR +
							CYP5525_UCSI_INTR);
		} else if (!strncmp(argv[1], "verbose", 7)) {
			verbose_msg_logging = (i != 0);
			CPRINTS("verbose=%d", verbose_msg_logging);
		} else if (!strncmp(argv[1], "ucsi", 4)) {
			ucsi_set_debug(i != 0);
			CPRINTS("ucsi verbose=%d", i);
		} else if (!strncmp(argv[1], "setpdo", 6)) {
			uint32_t pdo;
			if (argc < 4) {
				return EC_ERROR_PARAM3;
			}
			pdo = strtoul(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
			cypd_set_source_pdo(i, 0, &pdo, 1, 0);
			cypd_set_source_pdo(i, 1, &pdo, 1, 0);
		} else if (!strncmp(argv[1], "powerstate", 10)) {
			int pwrstate;
			if (argc < 4) {
				return EC_ERROR_PARAM3;
			}
			pwrstate = strtoul(argv[3], &e, 0);
			if (*e)
				return EC_ERROR_PARAM3;
			cypd_set_power_state(pwrstate);
		} else if (!strncmp(argv[1], "reg", 3)) {
			int r;
			int regval;
			if (argc < 5) {
				return EC_ERROR_PARAM4;
			}
			r = strtoul(argv[3], &e, 0);
			regval = strtoul(argv[4], &e, 0);
			cypd_write_reg16(i, r,  regval);
		} else {
			return EC_ERROR_PARAM1;
		}
	} else {
		return EC_ERROR_PARAM_COUNT;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(cypdctl, cmd_cypd_control,
			"[enable/disable/reset/clearint/verbose/ucsi] [controller] ",
			"Set if handling is active for controller");

static int cmd_cypd_bb(int argc, char **argv)
{
	uint32_t ctrl, cmd, data;
	char *e;
	if (argc == 2 && !strncmp(argv[1], "compliance", 10)) {
			enable_compliance_mode(0);
			enable_compliance_mode(1);
	} else if (argc == 4) {
		ctrl = strtoi(argv[1], &e, 0);
		if (*e || ctrl >= PD_CHIP_COUNT)
			return EC_ERROR_PARAM1;
		cmd = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		data = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;

		cypd_write_reg_block(ctrl, 0x48, (void *)&data, 4);
		cypd_write_reg16(ctrl, 0x46, cmd);
	}
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(cypdbb, cmd_cypd_bb,
			"controller 0x0000 0xdata ",
			"Write to the bb control register");


static int cmd_cypd_msg(int argc, char **argv)
{
	uint32_t sys_port, port, ctrl, cmd, data_len;
	uint16_t data[5];
	char *e;
	int chunked = 0;

	if (argc >= 2) {
	sys_port = strtoi(argv[1], &e, 0);
	if (*e || sys_port >= PD_PORT_COUNT)
		return EC_ERROR_PARAM1;
	}
	port = sys_port % 2;
	ctrl = sys_port / 2;
	if (argc >= 3) {
		if (argc >= 4) {
			chunked = strtoi(argv[3], &e, 0);
		}

		if (!strncmp(argv[2], "batterycap", 10)) {
			data[0] = PD_EXT_GET_BATTERY_CAP;/*ext msg type*/

		} else if (!strncmp(argv[2], "batterystatus", 13)) {
			data[0] = PD_EXT_GET_BATTERY_STATUS; /*ext msg type*/
		}
		/* ext msg header*/
		data[1] = 0x01; /*data size*/
		/*note when in chunked mode the first chunk does not set request_chunk=1
		 * see example 6.2.1.2.5.2 in usb pd rev 3.0
		 */
		data[1] |= chunked ? BIT(15) : 0x00;
		/*ext msg data*/
		data[2] = 0; /*internal battery 0*/
		data_len = 5;
		cypd_write_reg_block(ctrl, CYP5525_WRITE_DATA_MEMORY_REG(port, 0),
			(void *)data, 5);
		/*the request has 1 byte which should be set to 0 for battery idx 0*/
		cmd = CYP5525_DM_CTRL_SOP + CYP5525_DM_CTRL_EXTENDED_DATA_REQUEST + (data_len<<8);
		cypd_write_reg16(ctrl, CYP5525_DM_CONTROL_REG(port), cmd);
		CPRINTS("sent extended message");

	}
	return EC_SUCCESS;

}
DECLARE_CONSOLE_COMMAND(cypdmsg, cmd_cypd_msg,
			"port [batterycap|batterystatus] chunked=1,0",
			"Trigger extended message ams");
