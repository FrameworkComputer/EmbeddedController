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

#ifdef HAS_TASK_USB_CHG
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

#ifdef HAS_TASK_USB_CHG
static int bc12_detected_type[CONFIG_USB_PD_PORT_COUNT];

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
	case BD99955_TYPE_OTHER:
		return CHARGE_SUPPLIER_OTHER;
	case BD99955_TYPE_VBUS_OPEN:
	case BD99955_TYPE_PUP_PORT:
	case BD99955_TYPE_OPEN_PORT:
	default:
		return CHARGE_SUPPLIER_NONE;
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
}

static int bd99955_enable_vbus_detect_interrupts(int port, int enable)
{
	int reg;
	int rv;
	int port_reg;
	int mask_val;

	/* 1st Level Interrupt Setting */
	rv = ch_raw_read16(BD99955_CMD_INT0_SET, &reg,
			BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	mask_val = ((port == BD99955_CHARGE_PORT_VBUS) ?
			BD99955_CMD_INT0_SET_INT1_EN :
			BD99955_CMD_INT0_SET_INT2_EN) |
			BD99955_CMD_INT0_SET_INT0_EN;
	if (enable)
		reg |= mask_val;
	else
		reg &= ~mask_val;

	rv = ch_raw_write16(BD99955_CMD_INT0_SET, reg,
			BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* 2nd Level Interrupt Setting */
	port_reg = (port == BD99955_CHARGE_PORT_VBUS) ?
			BD99955_CMD_INT1_SET : BD99955_CMD_INT2_SET;
	rv = ch_raw_read16(port_reg, &reg, BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Enable threshold interrupts if we need to control discharge */
#ifdef CONFIG_USB_PD_DISCHARGE
	mask_val = BD99955_CMD_INT_VBUS_DET | BD99955_CMD_INT_VBUS_TH;
#else
	mask_val = BD99955_CMD_INT_VBUS_DET;
#endif
	if (enable)
		reg |= mask_val;
	else
		reg &= ~mask_val;

	return ch_raw_write16(port_reg, reg, BD99955_EXTENDED_COMMAND);
}

/* Read + clear active interrupt bits for a given port */
static int bd99955_get_interrupts(int port)
{
	int rv;
	int reg;
	int port_reg;

	port_reg = (port == BD99955_CHARGE_PORT_VBUS) ?
			BD99955_CMD_INT1_STATUS : BD99955_CMD_INT2_STATUS;

	rv = ch_raw_read16(port_reg, &reg, BD99955_EXTENDED_COMMAND);

	if (rv)
		return 0;

	/* Clear the interrupt status bits we just read */
	ch_raw_write16(port_reg, reg, BD99955_EXTENDED_COMMAND);

	return reg;
}

static void usb_charger_process(enum bd99955_charge_port port)
{
	int chg_port = bd99955_pd_port_to_chg_port(port);
	int vbus_provided = bd99955_is_vbus_provided(port) &&
			    !usb_charger_port_is_sourcing_vbus(chg_port);

	/* Inform other modules about VBUS level */
	usb_charger_vbus_change(chg_port, vbus_provided);

	/* Do BC1.2 detection */
	if (vbus_provided) {
		/* Charger/sync attached */
		bc12_detected_type[port] = bd99955_bc12_detect(port);
	} else if (bc12_detected_type[port] != CHARGE_SUPPLIER_NONE) {
		/* Charger/sync detached */
		bd99955_bc12_detach(port, bc12_detected_type[port]);
		bc12_detected_type[port] = CHARGE_SUPPLIER_NONE;
	}
}
#endif /* HAS_TASK_USB_CHG */

static int bd99955_set_vsysreg(int voltage)
{
	/* VSYS Regulation voltage is in 64mV steps. */
	voltage &= ~0x3F;

	return ch_raw_write16(BD99955_CMD_VSYSREG_SET, voltage,
			      BD99955_EXTENDED_COMMAND);
}

/* chip specific interfaces */

int charger_set_input_current(int input_current)
{
	int rv;

	/* Input current step 32 mA */
	input_current &= ~0x1F;

	if (input_current < bd99955_charger_info.input_current_min)
		input_current = bd99955_charger_info.input_current_min;

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

	if (mode & CHARGE_FLAG_INHIBIT_CHARGE) {
		rv = bd99955_set_vsysreg(BD99955_DISCHARGE_VSYSREG);
		msleep(50);
		rv |= bd99955_charger_enable(0);
	} else {
		rv = bd99955_charger_enable(1);
		msleep(1);
		rv |= bd99955_set_vsysreg(BD99955_CHARGE_VSYSREG);
	}
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
	int rv;

	/* Charge current step 64 mA */
	current &= ~0x3F;

	if (current < BD99955_NO_BATTERY_CHARGE_I_MIN &&
	    (battery_is_present() != BP_YES || battery_is_cut_off()))
		current = BD99955_NO_BATTERY_CHARGE_I_MIN;
	else if (current < bd99955_charger_info.current_min)
		current = bd99955_charger_info.current_min;

	rv = ch_raw_write16(BD99955_CMD_CHG_CURRENT, current,
			    BD99955_BAT_CHG_COMMAND);
	if (rv)
		return rv;

	return ch_raw_write16(BD99955_CMD_IPRECH_SET,
			      MIN(current, BD99955_IPRECH_MAX),
			      BD99955_EXTENDED_COMMAND);
}

int charger_get_voltage(int *voltage)
{
	return ch_raw_read16(BD99955_CMD_CHG_VOLTAGE, voltage,
				BD99955_BAT_CHG_COMMAND);
}

int charger_set_voltage(int voltage)
{
	int rv;
	int reg;
	const struct battery_info *bi = battery_get_info();

	/*
	 * Regulate the system voltage to battery max if the battery
	 * is not present or the battery is discharging on AC.
	 */
	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (reg & BD99955_CMD_CHGOP_SET2_BATT_LEARN ||
		battery_is_present() != BP_YES ||
		battery_is_cut_off())
		voltage = bi->voltage_max;

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
	int power_save_mode = BD99955_PWR_SAVE_OFF;
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

	/* TODO(crosbug.com/p/55626): Set  VSYSVAL_THH/THL appropriately */

	/* Set battery OVP to 500 + maximum battery voltage */
	ch_raw_write16(BD99955_CMD_VBATOVP_SET,
		       (bi->voltage_max + 500) & 0x7ff0,
		       BD99955_EXTENDED_COMMAND);

	/* Disable IADP pin current limit */
	if (ch_raw_read16(BD99955_CMD_VM_CTRL_SET, &reg,
			  BD99955_EXTENDED_COMMAND))
		return;
	reg &= ~BD99955_CMD_VM_CTRL_SET_EXTIADPEN;
	ch_raw_write16(BD99955_CMD_VM_CTRL_SET, reg,
		       BD99955_EXTENDED_COMMAND);

	/* Set fast charging watchdog to 1020 minutes. */
	ch_raw_write16(BD99955_CMD_CHGWDT_SET, 0xFF10,
		       BD99955_EXTENDED_COMMAND);

	/* Set charge termination current to 0 mA. */
	ch_raw_write16(BD99955_CMD_ITERM_SET, 0,
		       BD99955_EXTENDED_COMMAND);

	/* Set Pre-charge Voltage Threshold for trickle charging. */
	ch_raw_write16(BD99955_CMD_VPRECHG_TH_SET,
		       bi->voltage_min & 0x7FC0,
		       BD99955_EXTENDED_COMMAND);

	/* Trickle-charge Current Setting */
	ch_raw_write16(BD99955_CMD_ITRICH_SET,
		       bi->precharge_current & 0x07C0,
		       BD99955_EXTENDED_COMMAND);

	/* Power save mode when VBUS/VCC is removed. */
#ifdef CONFIG_BD99955_POWER_SAVE_MODE
	power_save_mode = CONFIG_BD99955_POWER_SAVE_MODE;
#endif
	ch_raw_write16(BD99955_CMD_SMBREG, power_save_mode,
		       BD99955_EXTENDED_COMMAND);

#ifdef CONFIG_USB_PD_DISCHARGE
	/* Set VBUS / VCC detection threshold for discharge enable */
	ch_raw_write16(BD99955_CMD_VBUS_TH_SET, BD99955_VBUS_DISCHARGE_TH,
		       BD99955_EXTENDED_COMMAND);
	ch_raw_write16(BD99955_CMD_VCC_TH_SET, BD99955_VBUS_DISCHARGE_TH,
		       BD99955_EXTENDED_COMMAND);
#endif
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

int charger_get_vbus_level(void)
{
	int vbus_val;
	int vcc_val;
	int rv;

	rv = ch_raw_read16(BD99955_CMD_VBUS_VAL, &vbus_val,
				BD99955_EXTENDED_COMMAND);

	rv += ch_raw_read16(BD99955_CMD_VCC_VAL, &vcc_val,
				BD99955_EXTENDED_COMMAND);

	return rv ? 0 : MAX(vbus_val, vcc_val);
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

#ifdef CONFIG_CHARGER_BATTERY_TSENSE
int bd99955_get_battery_temp(int *temp_ptr)
{
	int rv;

	rv = ch_raw_read16(BD99955_CMD_THERM_VAL, temp_ptr,
			BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Degrees C = 200 - THERM_VAL, range is -55C-200C, 1C steps */
	*temp_ptr = 200 - *temp_ptr;
	return EC_SUCCESS;
}
#endif

#ifdef HAS_TASK_USB_CHG
int bd99955_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_CDP:
		return 1500;
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 900;
	case CHARGE_SUPPLIER_OTHER:
#ifdef CONFIG_CHARGE_RAMP
		return 2400;
#else
		/*
		 * Setting the higher limit of current may result in an
		 * anti-collapse hence limiting the current to 1A.
		 */
		return 1000;
#endif
	default:
		return 500;
	}
}

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
	if (setting == usb_switch_state[port])
		return;

	if (setting != USB_SWITCH_RESTORE)
		usb_switch_state[port] = setting;
	bd99955_enable_usb_switch(port, usb_switch_state[port]);
}

void bd99955_vbus_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG);
}

void usb_charger_task(void)
{
	static int initialized;
	int changed, port, interrupts;
#ifdef CONFIG_USB_PD_DISCHARGE
	int vbus_reg, voltage;
#endif

	for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
		bc12_detected_type[port] = CHARGE_SUPPLIER_NONE;
		bd99955_enable_vbus_detect_interrupts(port, 1);
	}

	while (1) {
		changed = 0;
		for (port = 0; port < CONFIG_USB_PD_PORT_COUNT; port++) {
			/* Get port interrupts */
			interrupts = bd99955_get_interrupts(port);
			if (interrupts & BD99955_CMD_INT_VBUS_DET ||
			    !initialized) {
				/* Detect based on current state of VBUS */
				usb_charger_process(port);
				changed = 1;
			}
#ifdef CONFIG_USB_PD_DISCHARGE
			if (interrupts & BD99955_CMD_INT_VBUS_TH ||
			    !initialized) {
				/* Get VBUS voltage */
				vbus_reg = (port == BD99955_CHARGE_PORT_VBUS) ?
					   BD99955_CMD_VBUS_VAL :
					   BD99955_CMD_VCC_VAL;
				if (ch_raw_read16(vbus_reg,
						  &voltage,
						  BD99955_EXTENDED_COMMAND))
					voltage = 0;

				/* Set discharge accordingly */
				pd_set_vbus_discharge(
					bd99955_pd_port_to_chg_port(port),
					voltage < BD99955_VBUS_DISCHARGE_TH);
				changed = 1;
			}
#endif
		}

		initialized = 1;

		/*
		 * Re-read interrupt registers immediately if we got an
		 * interrupt. We're dealing with multiple independent
		 * interrupt sources and the interrupt pin may have
		 * never deasserted if both sources were not in clear
		 * state simultaneously.
		 */
		if (!changed)
			/* Wait for task wake */
			task_wait_event(-1);
	}
}
#endif /* HAS_TASK_USB_CHG */


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
	for (i = 0; i < 0x7f; ++i) {
		ccprintf("EXT REG %4x:  %4x\n", i, read_ext(i));
		cflush();
	}

	return 0;
}
DECLARE_CONSOLE_COMMAND(bd99955_dump, console_bd99955_dump_regs,
			NULL,
			"Dump all charger registers");

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

	if (rw == 'r')
		rv = ch_raw_read16(reg, &data, cmd);
	else {
		val = strtoi(argv[4], &e, 16);
		if (*e || val < 0)
			return EC_ERROR_PARAM4;

		rv = ch_raw_write16(reg, val, cmd);
		if (rv == EC_SUCCESS)
			rv = ch_raw_read16(reg, &data, cmd);
	}

	if (rv == EC_SUCCESS)
		CPRINTS("register 0x%x [%d] = 0x%x [%d]", reg, reg, data, data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(bd99955, console_command_bd99955,
			"bd99955 <r/w> <reg_hex> <cmd_type> | <val_hex>",
			"Read or write a charger register");
#endif /* CONFIG_CMD_CHARGER */

#ifdef CONFIG_CMD_CHARGER_PSYS
static int bd99955_psys_charger_adc(void)
{
	int i;
	int reg;
	uint64_t ipmon = 0;

	for (i = 0; i < BD99955_PMON_IOUT_ADC_READ_COUNT; i++) {
		if (ch_raw_read16(BD99955_CMD_PMON_DACIN_VAL, &reg,
				BD99955_EXTENDED_COMMAND))
			return 0;

		/* Conversion Interval is 200us */
		usleep(200);
		ipmon += reg;
	}

	/*
	 * Calculate power in mW
	 * PSYS = VACP×IACP+VBAT×IBAT = IPMON / GPMON
	 */
	return (int) ((ipmon * 1000) / ((1 << BD99955_PSYS_GAIN_SELECT) *
		BD99955_PMON_IOUT_ADC_READ_COUNT));
}

static int bd99955_enable_psys(void)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_PMON_IOUT_CTRL_SET, &reg,
			BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Enable PSYS & Select PSYS Gain */
	reg &= ~BD99955_CMD_PMON_IOUT_CTRL_SET_PMON_GAIN_SET_MASK;
	reg |= (BD99955_CMD_PMON_IOUT_CTRL_SET_PMON_INSEL |
		BD99955_CMD_PMON_IOUT_CTRL_SET_PMON_OUT_EN |
		BD99955_PSYS_GAIN_SELECT);

	return ch_raw_write16(BD99955_CMD_PMON_IOUT_CTRL_SET, reg,
			BD99955_EXTENDED_COMMAND);
}

/**
 * Get system power.
 */
static int console_command_psys(int argc, char **argv)
{
	int rv;

	rv = bd99955_enable_psys();
	if (rv)
		return rv;

	CPRINTS("PSYS from chg_adc: %d mW",
			bd99955_psys_charger_adc());

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(psys, console_command_psys,
			NULL,
			"Get the system power in mW");
#endif /* CONFIG_CMD_CHARGER_PSYS */

#ifdef CONFIG_CMD_CHARGER_ADC_AMON_BMON
static int bd99955_amon_bmon_chg_adc(void)
{
	int i;
	int reg;
	int iout = 0;

	for (i = 0; i < BD99955_PMON_IOUT_ADC_READ_COUNT; i++) {
		ch_raw_read16(BD99955_CMD_IOUT_DACIN_VAL, &reg,
				BD99955_EXTENDED_COMMAND);
		iout += reg;

		/* Conversion Interval is 200us */
		usleep(200);
	}

	/*
	 * Discharge current in mA
	 * IDCHG = iout * GIDCHG
	 * IADP = iout * GIADP
	 *
	 * VIDCHG = GIDCHG * (VSRN- VSRP) = GIDCHG * IDCHG / IDCHG_RES
	 * VIADP = GIADP * (VACP- VACN) = GIADP * IADP / IADP_RES
	 */
	return (iout * (5 << BD99955_IOUT_GAIN_SELECT)) /
		(10 * BD99955_PMON_IOUT_ADC_READ_COUNT);
}

static int bd99955_amon_bmon(int amon_bmon)
{
	int rv;
	int reg;
	int imon;
	int sns_res;

	rv = ch_raw_read16(BD99955_CMD_PMON_IOUT_CTRL_SET, &reg,
			BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Enable monitor */
	reg &= ~BD99955_CMD_PMON_IOUT_CTRL_SET_IOUT_GAIN_SET_MASK;
	reg |= (BD99955_CMD_PMON_IOUT_CTRL_SET_IMON_INSEL |
		BD99955_CMD_PMON_IOUT_CTRL_SET_IOUT_OUT_EN |
		(BD99955_IOUT_GAIN_SELECT << 4));

	if (amon_bmon) {
		reg |= BD99955_CMD_PMON_IOUT_CTRL_SET_IOUT_SOURCE_SEL;
		sns_res = CONFIG_CHARGER_SENSE_RESISTOR_AC;
	} else {
		reg &= ~BD99955_CMD_PMON_IOUT_CTRL_SET_IOUT_SOURCE_SEL;
		sns_res = CONFIG_CHARGER_SENSE_RESISTOR;
	}

	rv = ch_raw_write16(BD99955_CMD_PMON_IOUT_CTRL_SET, reg,
			BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	imon = bd99955_amon_bmon_chg_adc();

	CPRINTS("%cMON from chg_adc: %d uV, %d mA]",
		amon_bmon ? 'A' : 'B',
		imon * sns_res,
		imon);

	return EC_SUCCESS;
}

/**
 * Get charger AMON and BMON current.
 */
static int console_command_amon_bmon(int argc, char **argv)
{
	int rv = EC_ERROR_PARAM1;

	/* Switch to AMON */
	if (argc == 1 || (argc >= 2 && argv[1][0] == 'a'))
		rv = bd99955_amon_bmon(1);

	/* Switch to BMON */
	if (argc == 1 || (argc >= 2 && argv[1][0] == 'b'))
		rv = bd99955_amon_bmon(0);

	return rv;
}
DECLARE_CONSOLE_COMMAND(amonbmon, console_command_amon_bmon,
			"amonbmon [a|b]",
			"Get charger AMON/BMON voltage diff, current");
#endif /* CONFIG_CMD_CHARGER_ADC_AMON_BMON */
