/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ROHM BD99955 battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bd99955.h"
#include "charge_manager.h"
#include "charger.h"
#include "console.h"
#include "ec_commands.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "time.h"
#include "util.h"
#include "usb_charge.h"
#include "usb_pd.h"

#define OTPROM_LOAD_WAIT_RETRY	3

#define BD99955_CHARGE_PORT_COUNT 2

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* TODO: Add accurate timeout for detecting BC1.2 */
#define BC12_DETECT_RETRY	10

/* Charger parameters */
static const struct charger_info bd99955_charger_info = {
	.name         = CHARGER_NAME,
	.voltage_max  = CHARGE_V_MAX,
	.voltage_min  = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max  = CHARGE_I_MAX,
	.current_min  = CHARGE_I_MIN,
	.current_step = CHARGE_I_STEP,
	.input_current_max  = INPUT_I_MAX,
	.input_current_min  = INPUT_I_MIN,
	.input_current_step = INPUT_I_STEP,
};

/* Charge command code map */
static enum bd99955_command charger_map_cmd = BD99955_INVALID_COMMAND;

static struct mutex bd99955_map_mutex;

#if defined(HAS_TASK_USB_CHG_P0) || defined(HAS_TASK_USB_CHG_P1)
/* USB switch */
static enum usb_switch usb_switch_state[BD99955_CHARGE_PORT_COUNT] = {
	USB_SWITCH_DISCONNECT,
	USB_SWITCH_DISCONNECT,
};
#endif

static inline int ch_raw_read16(int cmd, int *param,
				enum bd99955_command map_cmd)
{
	int rv;

	/* Map the Charge command code to appropriate region */
	mutex_lock(&bd99955_map_mutex);
	if (charger_map_cmd != map_cmd) {
		rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
				 BD99955_CMD_MAP_SET, map_cmd);
		if (rv)
			goto bd99955_read_cleanup;

		charger_map_cmd = map_cmd;
	}

	rv = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, cmd, param);

bd99955_read_cleanup:
	mutex_unlock(&bd99955_map_mutex);

	return rv;
}

static inline int ch_raw_write16(int cmd, int param,
					enum bd99955_command map_cmd)
{
	int rv;

	/* Map the Charge command code to appropriate region */
	mutex_lock(&bd99955_map_mutex);
	if (charger_map_cmd != map_cmd) {
		rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
					BD99955_CMD_MAP_SET, map_cmd);
		if (rv)
			goto bd99955_write_cleanup;

		charger_map_cmd = map_cmd;
	}

	rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, cmd, param);

bd99955_write_cleanup:
	mutex_unlock(&bd99955_map_mutex);

	return rv;
}

/* BD99955 local interfaces */

static int bd99955_charger_enable(int enable)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (enable)
		reg |= BD99955_CMD_CHGOP_SET2_CHG_EN;
	else
		reg &= ~BD99955_CMD_CHGOP_SET2_CHG_EN;

	return ch_raw_write16(BD99955_CMD_CHGOP_SET2, reg,
				BD99955_EXTENDED_COMMAND);
}

static int bd99955_por_reset(void)
{
	int rv;
	int reg;
	int i;

	rv = ch_raw_write16(BD99955_CMD_SYSTEM_CTRL_SET,
			BD99955_CMD_SYSTEM_CTRL_SET_OTPLD |
			BD99955_CMD_SYSTEM_CTRL_SET_ALLRST,
			BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Wait until OTPROM loading is finished */
	for (i = 0; i < OTPROM_LOAD_WAIT_RETRY; i++) {
		msleep(10);
		rv = ch_raw_read16(BD99955_CMD_SYSTEM_STATUS, &reg,
				BD99955_EXTENDED_COMMAND);

		if (!rv && (reg & BD99955_CMD_SYSTEM_STATUS_OTPLD_STATE) &&
			(reg & BD99955_CMD_SYSTEM_STATUS_ALLRST_STATE))
			break;
	}

	if (rv)
		return rv;
	if (i == OTPROM_LOAD_WAIT_RETRY)
		return EC_ERROR_TIMEOUT;

	return ch_raw_write16(BD99955_CMD_SYSTEM_CTRL_SET, 0,
				BD99955_EXTENDED_COMMAND);
}

static int bd99955_reset_to_zero(void)
{
	int rv;

	rv = charger_set_current(0);
	if (rv)
		return rv;

	return charger_set_voltage(0);
}

static int bd99955_get_charger_op_status(int *status)
{
	return ch_raw_read16(BD99955_CMD_CHGOP_STATUS, status,
				BD99955_EXTENDED_COMMAND);
}

#if defined(HAS_TASK_USB_CHG_P0) || defined(HAS_TASK_USB_CHG_P1)
static int bd99955_get_bc12_device_type(enum bd99955_charge_port port)
{
	int rv;
	int reg;

	rv = ch_raw_read16((port == BD99955_CHARGE_PORT_VBUS) ?
				BD99955_CMD_VBUS_UCD_STATUS :
				BD99955_CMD_VCC_UCD_STATUS,
				&reg, BD99955_EXTENDED_COMMAND);
	if (rv)
		return CHARGE_SUPPLIER_NONE;

	switch (reg & BD99955_TYPE_MASK) {
	case BD99955_TYPE_CDP:
		return CHARGE_SUPPLIER_BC12_CDP;
	case BD99955_TYPE_DCP:
		return CHARGE_SUPPLIER_BC12_DCP;
	case BD99955_TYPE_SDP:
		return CHARGE_SUPPLIER_BC12_SDP;
	case BD99955_TYPE_VBUS_OPEN:
	case BD99955_TYPE_PUP_PORT:
	case BD99955_TYPE_OPEN_PORT:
	default:
		return CHARGE_SUPPLIER_NONE;
	}
}

static int bd99955_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_CDP:
		return 1500;
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 900;
	default:
		return 500;
	}
}

static int bd99955_enable_usb_switch(enum bd99955_charge_port port,
					enum usb_switch setting)
{
	int rv;
	int reg;
	int port_reg;

	port_reg = (port == BD99955_CHARGE_PORT_VBUS) ?
		BD99955_CMD_VBUS_UCD_SET : BD99955_CMD_VCC_UCD_SET;

	rv = ch_raw_read16(port_reg, &reg, BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (setting == USB_SWITCH_CONNECT)
		reg |= BD99955_CMD_UCD_SET_USB_SW_EN;
	else
		reg &= ~BD99955_CMD_UCD_SET_USB_SW_EN;

	return ch_raw_write16(port_reg, reg, BD99955_EXTENDED_COMMAND);
}

static int bd99955_bc12_detect(int port)
{
	int i;
	int bc12_type;
	struct charge_port_info charge;

	/*
	 * BC1.2 detection starts 100ms after VBUS/VCC attach and typically
	 * completes 312ms after VBUS/VCC attach.
	 */
	msleep(312);
	for (i = 0; i < BC12_DETECT_RETRY; i++) {
		/* get device type */
		bc12_type = bd99955_get_bc12_device_type(port);

		/* Detected BC1.2 */
		if (bc12_type != CHARGE_SUPPLIER_NONE)
			break;

		/* TODO: Add accurate timeout for detecting BC1.2 */
		msleep(100);
	}

	/* BC1.2 device attached */
	if (bc12_type != CHARGE_SUPPLIER_NONE) {
		/* Update charge manager */
		charge.voltage = USB_CHARGER_VOLTAGE_MV;
		charge.current = bd99955_get_bc12_ilim(bc12_type);
		charge_manager_update_charge(bc12_type, port, &charge);

		/* notify host of power info change */
		pd_send_host_event(PD_EVENT_POWER_CHANGE);
	}

	return bc12_type;
}

static void bd99955_bc12_detach(int port, int type)
{
	struct charge_port_info charge = {
		.voltage = USB_CHARGER_VOLTAGE_MV,
		.current = 0,
	};

	/* Update charge manager */
	charge_manager_update_charge(type, port, &charge);

	/* Disable charging trigger by BC1.2 detection */
	bd99955_bc12_enable_charging(port, 0);

	/* notify host of power info change */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}
#endif /* defined(HAS_TASK_USB_CHG_P0) || defined(HAS_TASK_USB_CHG_P1) */


/* chip specific interfaces */

int charger_set_input_current(int input_current)
{
	int rv;

	/* Input current step 32 mA */
	input_current &= ~0x1F;
	rv = ch_raw_write16(BD99955_CMD_IBUS_LIM_SET, input_current,
				BD99955_BAT_CHG_COMMAND);
	if (rv)
		return rv;

	return ch_raw_write16(BD99955_CMD_ICC_LIM_SET, input_current,
				BD99955_BAT_CHG_COMMAND);
}

int charger_get_input_current(int *input_current)
{
	return ch_raw_read16(BD99955_CMD_CUR_ILIM_VAL, input_current,
			     BD99955_EXTENDED_COMMAND);
}

int charger_manufacturer_id(int *id)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_device_id(int *id)
{
	return ch_raw_read16(BD99955_CMD_CHIP_ID, id, BD99955_EXTENDED_COMMAND);
}

int charger_get_option(int *option)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET1, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	*option = reg;
	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	*option |= reg << 16;

	return EC_SUCCESS;
}

int charger_set_option(int option)
{
	int rv;

	rv = ch_raw_write16(BD99955_CMD_CHGOP_SET1, option & 0xFFFF,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	return ch_raw_write16(BD99955_CMD_CHGOP_SET2, (option >> 16) & 0xFFFF,
				BD99955_EXTENDED_COMMAND);
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &bd99955_charger_info;
}

int charger_get_status(int *status)
{
	int rv;
	int reg;
	int ch_status;

	/* charger level */
	*status = CHARGER_LEVEL_2;

	/* charger enable/inhibit */
	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (!(reg & BD99955_CMD_CHGOP_SET2_CHG_EN))
		*status |= CHARGER_CHARGE_INHIBITED;

	/* charger alarm enable/inhibit */
	rv = ch_raw_read16(BD99955_CMD_PROCHOT_CTRL_SET, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (!(reg & (BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN4 |
			BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN3 |
			BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN2 |
			BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN1 |
			BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN0)))
		*status |= CHARGER_ALARM_INHIBITED;

	rv = bd99955_get_charger_op_status(&reg);
	if (rv)
		return rv;

	/* power fail */
	if (!(reg & BD99955_CMD_CHGOP_STATUS_RBOOST_UV))
		*status |= CHARGER_POWER_FAIL;

	/* Safety signal ranges & battery presence */
	ch_status = (reg & BD99955_BATTTEMP_MASK) >> 8;

	*status |= CHARGER_BATTERY_PRESENT;

	switch (ch_status) {
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_COLD1:
		*status |= CHARGER_RES_COLD;
		break;
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_COLD2:
		*status |= CHARGER_RES_COLD;
		*status |= CHARGER_RES_UR;
		break;
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_HOT1:
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_HOT2:
		*status |= CHARGER_RES_HOT;
		break;
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_HOT3:
		*status |= CHARGER_RES_HOT;
		*status |= CHARGER_RES_OR;
		break;
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_BATOPEN:
		*status &= ~CHARGER_BATTERY_PRESENT;
	default:
		break;
	}

	/* source of power */
	if (bd99955_is_vbus_provided(BD99955_CHARGE_PORT_BOTH))
		*status |= CHARGER_AC_PRESENT;

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	int rv;

	rv = bd99955_charger_enable(mode & CHARGE_FLAG_INHIBIT_CHARGE ? 0 : 1);
	if (rv)
		return rv;

	if (mode & CHARGE_FLAG_POR_RESET) {
		rv = bd99955_por_reset();
		if (rv)
			return rv;
	}

	if (mode & CHARGE_FLAG_RESET_TO_ZERO) {
		rv = bd99955_reset_to_zero();
		if (rv)
			return rv;
	}

	return EC_SUCCESS;
}

int charger_get_current(int *current)
{
	return ch_raw_read16(BD99955_CMD_CHG_CURRENT, current,
				BD99955_BAT_CHG_COMMAND);
}

int charger_set_current(int current)
{
	/* Charge current step 64 mA */
	current &= ~0x3F;

	if (current < bd99955_charger_info.current_min)
		current = bd99955_charger_info.current_min;

	return ch_raw_write16(BD99955_CMD_CHG_CURRENT, current,
				BD99955_BAT_CHG_COMMAND);
}

int charger_get_voltage(int *voltage)
{
	return ch_raw_read16(BD99955_CMD_CHG_VOLTAGE, voltage,
				BD99955_BAT_CHG_COMMAND);
}

int charger_set_voltage(int voltage)
{
	/* Charge voltage step 16 mV */
	voltage &= ~0x0F;

	if (voltage < bd99955_charger_info.voltage_min)
		voltage = bd99955_charger_info.voltage_min;

	return ch_raw_write16(BD99955_CMD_CHG_VOLTAGE, voltage,
				BD99955_BAT_CHG_COMMAND);
}

static void bd99995_init(void)
{
	int reg;
	const struct battery_info *bi = battery_get_info();

	/* Enable BC1.2 detection on VCC */
	if (ch_raw_read16(BD99955_CMD_VCC_UCD_SET, &reg,
			  BD99955_EXTENDED_COMMAND))
		return;
	reg |= BD99955_CMD_UCD_SET_USBDETEN;
	reg &= ~BD99955_CMD_UCD_SET_USB_SW_EN;
	ch_raw_write16(BD99955_CMD_VCC_UCD_SET, reg,
		       BD99955_EXTENDED_COMMAND);

	/* Enable BC1.2 detection on VBUS */
	if (ch_raw_read16(BD99955_CMD_VBUS_UCD_SET, &reg,
			  BD99955_EXTENDED_COMMAND))
		return;
	reg |= BD99955_CMD_UCD_SET_USBDETEN;
	reg &= ~BD99955_CMD_UCD_SET_USB_SW_EN;
	ch_raw_write16(BD99955_CMD_VBUS_UCD_SET, reg,
		       BD99955_EXTENDED_COMMAND);

	/* Disable charging trigger by BC1.2 on VCC & VBUS. */
	if (ch_raw_read16(BD99955_CMD_CHGOP_SET1, &reg,
			  BD99955_EXTENDED_COMMAND))
		return;
	reg |= (BD99955_CMD_CHGOP_SET1_SDP_CHG_TRIG_EN |
		BD99955_CMD_CHGOP_SET1_SDP_CHG_TRIG |
		BD99955_CMD_CHGOP_SET1_VBUS_BC_DISEN |
		BD99955_CMD_CHGOP_SET1_VCC_BC_DISEN |
		BD99955_CMD_CHGOP_SET1_ILIM_AUTO_DISEN);
	ch_raw_write16(BD99955_CMD_CHGOP_SET1, reg,
		       BD99955_EXTENDED_COMMAND);

	/* Enable BC1.2 USB charging and DC/DC converter */
	if (ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
			  BD99955_EXTENDED_COMMAND))
		return;
	reg &= ~(BD99955_CMD_CHGOP_SET2_USB_SUS);
	ch_raw_write16(BD99955_CMD_CHGOP_SET2, reg,
		       BD99955_EXTENDED_COMMAND);

	/* Set battery OVP to 500 + maximum battery voltage */
	ch_raw_write16(BD99955_CMD_VBATOVP_SET,
		       (bi->voltage_max + 500) & 0x7ff0,
		       BD99955_EXTENDED_COMMAND);
}
DECLARE_HOOK(HOOK_INIT, bd99995_init, HOOK_PRIO_INIT_EXTPOWER);

int charger_post_init(void)
{
	return EC_SUCCESS;
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (enable)
		reg |= BD99955_CMD_CHGOP_SET2_BATT_LEARN;
	else
		reg &= ~BD99955_CMD_CHGOP_SET2_BATT_LEARN;

	return ch_raw_write16(BD99955_CMD_CHGOP_SET2, reg,
				BD99955_EXTENDED_COMMAND);
}

/*** Non-standard interface functions ***/

int bd99955_is_vbus_provided(int port)
{
	int reg;

	if (ch_raw_read16(BD99955_CMD_VBUS_VCC_STATUS, &reg,
			  BD99955_EXTENDED_COMMAND))
		return 0;

	if (port == BD99955_CHARGE_PORT_VBUS)
		reg &= BD99955_CMD_VBUS_VCC_STATUS_VBUS_DETECT;
	else if (port == BD99955_CHARGE_PORT_VCC)
		reg &= BD99955_CMD_VBUS_VCC_STATUS_VCC_DETECT;
	else if (port == BD99955_CHARGE_PORT_BOTH) {
		/* Check VBUS on either port */
		reg &= (BD99955_CMD_VBUS_VCC_STATUS_VCC_DETECT |
			BD99955_CMD_VBUS_VCC_STATUS_VBUS_DETECT);
	} else
		reg = 0;

	return !!reg;
}

int bd99955_select_input_port(enum bd99955_charge_port port)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_VIN_CTRL_SET, &reg,
			   BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (port == BD99955_CHARGE_PORT_NONE) {
		reg &= ~(BD99955_CMD_VIN_CTRL_SET_VBUS_EN |
			 BD99955_CMD_VIN_CTRL_SET_VCC_EN);
	} else if (port == BD99955_CHARGE_PORT_VBUS) {
		reg |= BD99955_CMD_VIN_CTRL_SET_VBUS_EN;
		reg &= ~BD99955_CMD_VIN_CTRL_SET_VCC_EN;
	} else if (port == BD99955_CHARGE_PORT_VCC) {
		reg |= BD99955_CMD_VIN_CTRL_SET_VCC_EN;
		reg &= ~BD99955_CMD_VIN_CTRL_SET_VBUS_EN;
	} else if (port == BD99955_CHARGE_PORT_BOTH) {
		/* Enable both the ports for PG3 */
		reg |= BD99955_CMD_VIN_CTRL_SET_VBUS_EN |
			BD99955_CMD_VIN_CTRL_SET_VCC_EN;
	} else {
		/* Invalid charge port */
		panic("Invalid charge port");
	}

	return ch_raw_write16(BD99955_CMD_VIN_CTRL_SET, reg,
			      BD99955_EXTENDED_COMMAND);
}

#if defined(HAS_TASK_USB_CHG_P0) || defined(HAS_TASK_USB_CHG_P1)
int bd99955_bc12_enable_charging(enum bd99955_charge_port port, int enable)
{
	int rv;
	int reg;
	int mask_val;

	/*
	 * For BC1.2, enable VBUS/VCC_BC_DISEN charging trigger by BC1.2
	 * detection and disable SDP_CHG_TRIG, SDP_CHG_TRIG_EN. Vice versa
	 * for USB-C.
	 */
	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET1, &reg,
			BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	mask_val = (BD99955_CMD_CHGOP_SET1_SDP_CHG_TRIG_EN |
		BD99955_CMD_CHGOP_SET1_SDP_CHG_TRIG |
		((port == BD99955_CHARGE_PORT_VBUS) ?
		BD99955_CMD_CHGOP_SET1_VBUS_BC_DISEN :
		BD99955_CMD_CHGOP_SET1_VCC_BC_DISEN));

	if (enable)
		reg &= ~mask_val;
	else
		reg |= mask_val;

	return ch_raw_write16(BD99955_CMD_CHGOP_SET1, reg,
			BD99955_EXTENDED_COMMAND);
}

void usb_charger_set_switches(int port, enum usb_switch setting)
{
	/* If switch is not changing then return */
	if (setting == usb_switch_state[port] ||
		pd_snk_is_vbus_provided(port))
		return;

	if (setting != USB_SWITCH_RESTORE)
		usb_switch_state[port] = setting;
	bd99955_enable_usb_switch(port, usb_switch_state[port]);
}

void usb_charger_task(void)
{
	int port = (task_get_current() == TASK_ID_USB_CHG_P0 ? 0 : 1);
	int bc12_type = CHARGE_SUPPLIER_NONE;
	int vbus_provided;

	while (1) {
		vbus_provided = pd_snk_is_vbus_provided(port);

		if (vbus_provided) {
			/* Charger/sync attached */
			bc12_type = bd99955_bc12_detect(port);
		} else if (bc12_type != CHARGE_SUPPLIER_NONE &&
				!vbus_provided) {
			/* Charger/sync detached */
			bd99955_bc12_detach(port, bc12_type);
			bc12_type = CHARGE_SUPPLIER_NONE;
		}

		/* Wait for interrupt */
		task_wait_event(-1);
	}
}
#endif /* defined(HAS_TASK_USB_CHG_P0) || defined(HAS_TASK_USB_CHG_P1) */


/*** Console commands ***/

#ifdef CONFIG_CMD_CHARGER
static int read_bat(uint8_t cmd)
{
	int read = 0;

	ch_raw_read16(cmd, &read, BD99955_BAT_CHG_COMMAND);
	return read;
}

static int read_ext(uint8_t cmd)
{
	int read = 0;

	ch_raw_read16(cmd, &read, BD99955_EXTENDED_COMMAND);
	return read;
}

/* Dump all readable registers on bd99955 */
static int console_bd99955_dump_regs(int argc, char **argv)
{
	int i;
	uint8_t regs[] = { 0x14, 0x15, 0x3c, 0x3d, 0x3e, 0x3f };

	/* Battery group registers */
	for (i = 0; i < ARRAY_SIZE(regs); ++i)
		ccprintf("BAT REG %4x:  %4x\n", regs[i], read_bat(regs[i]));

	/* Extended group registers */
	for (i = 0; i < 0x7f; ++i)
		ccprintf("EXT REG %4x:  %4x\n", i, read_ext(i));

	return 0;
}
DECLARE_CONSOLE_COMMAND(bd99955_dump, console_bd99955_dump_regs,
			NULL,
			"Dump all charger registers",
			NULL);

static int console_command_bd99955(int argc, char **argv)
{
	int rv, reg, data, val;
	char rw, *e;
	enum bd99955_command cmd;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	rw = argv[1][0];
	if (rw == 'w' && argc < 5)
		return EC_ERROR_PARAM_COUNT;
	else if (rw != 'w' && rw != 'r')
		return EC_ERROR_PARAM1;

	reg = strtoi(argv[2], &e, 16);
	if (*e || reg < 0)
		return EC_ERROR_PARAM2;

	cmd = strtoi(argv[3], &e, 0);
	if (*e || cmd < 0)
		return EC_ERROR_PARAM3;

	if (argc == 5) {
		val = strtoi(argv[4], &e, 16);
		if (*e || val < 0)
			return EC_ERROR_PARAM4;
	}

	if (rw == 'r')
		rv = ch_raw_read16(reg, &data, cmd);
	else {
		rv = ch_raw_write16(reg, val, cmd);
		if (rv == EC_SUCCESS)
			rv = ch_raw_read16(reg, &data, cmd);
	}

	CPRINTS("register 0x%x [%d] = 0x%x [%d]", reg, reg, data, data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(bd99955, console_command_bd99955,
			"bd99955 <r/w> <reg_hex> <cmd_type> | <val_hex>",
			"Read or write a charger register",
			NULL);
#endif /* CONFIG_CMD_CHARGER */
