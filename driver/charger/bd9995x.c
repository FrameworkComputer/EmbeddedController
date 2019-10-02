/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ROHM BD9995X battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bd9995x.h"
#include "charge_manager.h"
#include "charge_state.h"
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

#define BD9995X_CHARGE_PORT_COUNT 2

/*
 * BC1.2 detection starts 100ms after VBUS/VCC attach and typically
 * completes 312ms after VBUS/VCC attach.
 */
#define BC12_DETECT_US (312*MSEC)
#define BD9995X_VSYS_PRECHARGE_OFFSET_MV 200

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#ifdef CONFIG_BD9995X_DELAY_INPUT_PORT_SELECT
/*
 * Used in a check to determine if VBUS is within the
 * range of some VOLTAGE +/- VBUS_DELTA, where voltage
 * is measured in mV.
 */
#define VBUS_DELTA 1000

/* VBUS is debounced if it's stable for this length of time */
#define VBUS_MSEC (100*MSEC)

/* VBUS debouncing sample interval */
#define VBUS_CHECK_MSEC (10*MSEC)

/* Time to wait before VBUS debouncing begins */
#define STABLE_TIMEOUT (500*MSEC)

/* Maximum time to wait until VBUS is debounced */
#define DEBOUNCE_TIMEOUT (500*MSEC)

enum vstate {START, STABLE, DEBOUNCE};
static enum vstate vbus_state;

static int vbus_voltage;
static uint64_t debounce_time;
static uint64_t vbus_timeout;
static int port_update;
static int select_update;
static int select_input_port_update;
#endif

/* Charger parameters */
static const struct charger_info bd9995x_charger_info = {
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
static enum bd9995x_command charger_map_cmd = BD9995X_INVALID_COMMAND;

/* Mutex for active register set control. */
static struct mutex bd9995x_map_mutex;

/* Tracks the state of VSYS_PRIORITY */
static int vsys_priority;
/* Mutex for VIN_CTRL_SET register */
static struct mutex bd9995x_vin_mutex;

#ifdef HAS_TASK_USB_CHG
/* USB switch */
static enum usb_switch usb_switch_state[BD9995X_CHARGE_PORT_COUNT] = {
	USB_SWITCH_DISCONNECT,
	USB_SWITCH_DISCONNECT,
};

static int bd9995x_get_bc12_ilim(int charge_supplier)
{
	switch (charge_supplier) {
	case CHARGE_SUPPLIER_BC12_CDP:
		return 1500;
	case CHARGE_SUPPLIER_BC12_DCP:
		return 2000;
	case CHARGE_SUPPLIER_BC12_SDP:
		return 900;
	case CHARGE_SUPPLIER_OTHER:
#ifdef CONFIG_CHARGE_RAMP_SW
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
#endif /* HAS_TASK_USB_CHG */

static inline int ch_raw_read16(int cmd, int *param,
				enum bd9995x_command map_cmd)
{
	int rv;

	/* Map the Charge command code to appropriate region */
	mutex_lock(&bd9995x_map_mutex);
	if (charger_map_cmd != map_cmd) {
		rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
				 BD9995X_CMD_MAP_SET, map_cmd);
		if (rv) {
			charger_map_cmd = BD9995X_INVALID_COMMAND;
			goto bd9995x_read_cleanup;
		}

		charger_map_cmd = map_cmd;
	}

	rv = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			cmd, param);

bd9995x_read_cleanup:
	mutex_unlock(&bd9995x_map_mutex);

	return rv;
}

static inline int ch_raw_write16(int cmd, int param,
					enum bd9995x_command map_cmd)
{
	int rv;

	/* Map the Charge command code to appropriate region */
	mutex_lock(&bd9995x_map_mutex);
	if (charger_map_cmd != map_cmd) {
		rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
				 BD9995X_CMD_MAP_SET, map_cmd);
		if (rv) {
			charger_map_cmd = BD9995X_INVALID_COMMAND;
			goto bd9995x_write_cleanup;
		}

		charger_map_cmd = map_cmd;
	}

	rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			 cmd, param);

bd9995x_write_cleanup:
	mutex_unlock(&bd9995x_map_mutex);

	return rv;
}

/* BD9995X local interfaces */

static int bd9995x_set_vfastchg(int voltage)
{

	int rv;

	/* Fast Charge Voltage Regulation Settings for fast charging. */
	rv = ch_raw_write16(BD9995X_CMD_VFASTCHG_REG_SET1,
			voltage & 0x7FF0, BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

#ifndef CONFIG_CHARGER_BATTERY_TSENSE
	/*
	 * If TSENSE is not connected set all the VFASTCHG_REG_SETx
	 * to same voltage.
	 */
	rv = ch_raw_write16(BD9995X_CMD_VFASTCHG_REG_SET2,
			voltage & 0x7FF0, BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	rv = ch_raw_write16(BD9995X_CMD_VFASTCHG_REG_SET3,
			voltage & 0x7FF0, BD9995X_EXTENDED_COMMAND);
#endif

	return rv;
}

static int bd9995x_set_vsysreg(int voltage)
{
	/* VSYS Regulation voltage is in 64mV steps. */
	voltage &= ~0x3F;

	return ch_raw_write16(BD9995X_CMD_VSYSREG_SET, voltage,
			      BD9995X_EXTENDED_COMMAND);
}

static int bd9995x_is_discharging_on_ac(void)
{
	int reg;

	if (ch_raw_read16(BD9995X_CMD_CHGOP_SET2, &reg,
				BD9995X_EXTENDED_COMMAND))
		return 0;

	return !!(reg & BD9995X_CMD_CHGOP_SET2_BATT_LEARN);
}

static int bd9995x_charger_enable(int enable)
{
	int rv, reg;
	static int prev_chg_enable = -1;
	const struct battery_info *bi = battery_get_info();

#ifdef CONFIG_CHARGER_BD9995X_CHGEN
	/*
	 * If the battery is not yet initialized, dont turn-off the BGATE so
	 * that voltage from the AC is applied to the battery PACK.
	 */
	if (!enable && !board_battery_initialized())
		return EC_SUCCESS;
#endif

	/* Nothing to change */
	if (enable == prev_chg_enable)
		return EC_SUCCESS;

	prev_chg_enable = enable;

	if (enable) {
		/*
		 * BGATE capacitor max : 0.1uF + 20%
		 * Charge MOSFET threshold max : 2.8V
		 * BGATE charge pump current min : 3uA
		 * T = C * V / I so, Tmax = 112ms
		 */
		msleep(115);

		/*
		 * Set VSYSREG_SET <= VBAT so that the charger is in Fast-Charge
		 * state when charging.
		 */
		rv = bd9995x_set_vsysreg(bi->voltage_min);
	} else {
		/*
		 * Set VSYSREG_SET > VBAT so that the charger is in Pre-Charge
		 * state when not charging or discharging.
		 */
		rv = bd9995x_set_vsysreg(bi->voltage_max +
					 BD9995X_VSYS_PRECHARGE_OFFSET_MV);

		/*
		 * Allow charger in pre-charge state for 50ms before disabling
		 * the charger which prevents inrush current while moving from
		 * fast-charge state to pre-charge state.
		 */
		msleep(50);
	}
	if (rv)
		return rv;

	rv = ch_raw_read16(BD9995X_CMD_CHGOP_SET2, &reg,
				BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (enable)
		reg |= BD9995X_CMD_CHGOP_SET2_CHG_EN;
	else
		reg &= ~BD9995X_CMD_CHGOP_SET2_CHG_EN;

	return ch_raw_write16(BD9995X_CMD_CHGOP_SET2, reg,
				BD9995X_EXTENDED_COMMAND);
}

static int bd9995x_por_reset(void)
{
	int rv;
	int reg;
	int i;

	rv = ch_raw_write16(BD9995X_CMD_SYSTEM_CTRL_SET,
			BD9995X_CMD_SYSTEM_CTRL_SET_OTPLD |
			BD9995X_CMD_SYSTEM_CTRL_SET_ALLRST,
			BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Wait until OTPROM loading is finished */
	for (i = 0; i < OTPROM_LOAD_WAIT_RETRY; i++) {
		msleep(10);
		rv = ch_raw_read16(BD9995X_CMD_SYSTEM_STATUS, &reg,
				BD9995X_EXTENDED_COMMAND);

		if (!rv && (reg & BD9995X_CMD_SYSTEM_STATUS_OTPLD_STATE) &&
			(reg & BD9995X_CMD_SYSTEM_STATUS_ALLRST_STATE))
			break;
	}

	if (rv)
		return rv;
	if (i == OTPROM_LOAD_WAIT_RETRY)
		return EC_ERROR_TIMEOUT;

	return ch_raw_write16(BD9995X_CMD_SYSTEM_CTRL_SET, 0,
				BD9995X_EXTENDED_COMMAND);
}

static int bd9995x_reset_to_zero(void)
{
	int rv;

	rv = charger_set_current(0);
	if (rv)
		return rv;

	return charger_set_voltage(0);
}

static int bd9995x_get_charger_op_status(int *status)
{
	return ch_raw_read16(BD9995X_CMD_CHGOP_STATUS, status,
				BD9995X_EXTENDED_COMMAND);
}

#ifdef HAS_TASK_USB_CHG
static int bc12_detected_type[CONFIG_USB_PD_PORT_MAX_COUNT];
/* Mutex for UCD_SET regsiters, lock before read / mask / write. */
static struct mutex ucd_set_mutex[BD9995X_CHARGE_PORT_COUNT];

static int bd9995x_get_bc12_device_type(int port)
{
	int rv;
	int reg;

	rv = ch_raw_read16((port == BD9995X_CHARGE_PORT_VBUS) ?
				BD9995X_CMD_VBUS_UCD_STATUS :
				BD9995X_CMD_VCC_UCD_STATUS,
				&reg, BD9995X_EXTENDED_COMMAND);
	if (rv)
		return CHARGE_SUPPLIER_NONE;

	switch (reg & BD9995X_TYPE_MASK) {
	case BD9995X_TYPE_CDP:
		return CHARGE_SUPPLIER_BC12_CDP;
	case BD9995X_TYPE_DCP:
		return CHARGE_SUPPLIER_BC12_DCP;
	case BD9995X_TYPE_SDP:
		return CHARGE_SUPPLIER_BC12_SDP;
	case BD9995X_TYPE_PUP_PORT:
	case BD9995X_TYPE_OTHER:
		return CHARGE_SUPPLIER_OTHER;
	case BD9995X_TYPE_OPEN_PORT:
	case BD9995X_TYPE_VBUS_OPEN:
	default:
		return CHARGE_SUPPLIER_NONE;
	}
}

/*
 * Do safe read / mask / write of BD9995X_CMD_*_UCD_SET register.
 * The USB charger task owns all bits of this register, except for bit 0
 * (BD9995X_CMD_UCD_SET_USB_SW), which is controlled by
 * usb_charger_set_switches().
 */
static int bd9995x_update_ucd_set_reg(int port, uint16_t mask, int set)
{
	int rv;
	int reg;
	int port_reg = (port == BD9995X_CHARGE_PORT_VBUS) ?
		BD9995X_CMD_VBUS_UCD_SET : BD9995X_CMD_VCC_UCD_SET;

	mutex_lock(&ucd_set_mutex[port]);
	rv = ch_raw_read16(port_reg, &reg, BD9995X_EXTENDED_COMMAND);
	if (!rv) {
		if (set)
			reg |= mask;
		else
			reg &= ~mask;

		rv = ch_raw_write16(port_reg, reg, BD9995X_EXTENDED_COMMAND);
	}

	mutex_unlock(&ucd_set_mutex[port]);
	return rv;
}

static int bd9995x_bc12_check_type(int port)
{
	int bc12_type;
	struct charge_port_info charge;
	int vbus_provided = bd9995x_is_vbus_provided(port) &&
			    !usb_charger_port_is_sourcing_vbus(port);

	/*
	 * If vbus is no longer provided, then no need to continue. Return 0 so
	 * that a wait event is not scheduled.
	 */
	if (!vbus_provided)
		return 0;

	/* get device type */
	bc12_type = bd9995x_get_bc12_device_type(port);
	if (bc12_type == CHARGE_SUPPLIER_NONE)
		/*
		 * Device type is not available, return non-zero so new wait
		 * will be scheduled before putting the task to sleep.
		 */
		return 1;

	bc12_detected_type[port] = bc12_type;
	/* Update charge manager */
	charge.voltage = USB_CHARGER_VOLTAGE_MV;
	charge.current = bd9995x_get_bc12_ilim(bc12_type);
	charge_manager_update_charge(bc12_type, port, &charge);

	return 0;
}

static void bd9995x_bc12_detach(int port, int type)
{
	/* Update charge manager */
	charge_manager_update_charge(type, port, NULL);

	/* Disable charging trigger by BC1.2 detection */
	bd9995x_bc12_enable_charging(port, 0);
}

static int bd9995x_enable_vbus_detect_interrupts(int port, int enable)
{
	int reg;
	int rv;
	int port_reg;
	int mask_val;

	/* 1st Level Interrupt Setting */
	rv = ch_raw_read16(BD9995X_CMD_INT0_SET, &reg,
			BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	mask_val = ((port == BD9995X_CHARGE_PORT_VBUS) ?
			BD9995X_CMD_INT0_SET_INT1_EN :
			BD9995X_CMD_INT0_SET_INT2_EN) |
			BD9995X_CMD_INT0_SET_INT0_EN;
	if (enable)
		reg |= mask_val;
	else
		reg &= ~mask_val;

	rv = ch_raw_write16(BD9995X_CMD_INT0_SET, reg,
			BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* 2nd Level Interrupt Setting */
	port_reg = (port == BD9995X_CHARGE_PORT_VBUS) ?
			BD9995X_CMD_INT1_SET : BD9995X_CMD_INT2_SET;
	rv = ch_raw_read16(port_reg, &reg, BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Enable threshold interrupts if we need to control discharge */
#ifdef CONFIG_USB_PD_DISCHARGE
	mask_val = BD9995X_CMD_INT_VBUS_DET | BD9995X_CMD_INT_VBUS_TH;
#else
	mask_val = BD9995X_CMD_INT_VBUS_DET;
#endif
	if (enable)
		reg |= mask_val;
	else
		reg &= ~mask_val;

	return ch_raw_write16(port_reg, reg, BD9995X_EXTENDED_COMMAND);
}

/* Read + clear active interrupt bits for a given port */
static int bd9995x_get_interrupts(int port)
{
	int rv;
	int reg;
	int port_reg;

	port_reg = (port == BD9995X_CHARGE_PORT_VBUS) ?
			BD9995X_CMD_INT1_STATUS : BD9995X_CMD_INT2_STATUS;

	rv = ch_raw_read16(port_reg, &reg, BD9995X_EXTENDED_COMMAND);

	if (rv)
		return 0;

	/* Clear the interrupt status bits we just read */
	ch_raw_write16(port_reg, reg, BD9995X_EXTENDED_COMMAND);

	return reg;
}

/*
 * Set or clear registers necessary to do one-time BC1.2 detection.
 * Pass enable = 1 to trigger BC1.2 detection, and enable = 0 once
 * BC1.2 detection has completed.
 */
static int bd9995x_bc12_detect(int port, int enable)
{
	return bd9995x_update_ucd_set_reg(port,
					  BD9995X_CMD_UCD_SET_BCSRETRY |
					  BD9995X_CMD_UCD_SET_USBDETEN |
					  BD9995X_CMD_UCD_SET_USB_SW_EN,
					  enable);
}

static int usb_charger_process(int port)
{
	int vbus_provided = bd9995x_is_vbus_provided(port) &&
			    !usb_charger_port_is_sourcing_vbus(port);

	/* Inform other modules about VBUS level */
	usb_charger_vbus_change(port, vbus_provided);

	/*
	 * Do BC1.2 detection, if we have VBUS and our port is not known
	 * to speak PD.
	 */
	if (vbus_provided && !pd_capable(port)) {
		bd9995x_bc12_detect(port, 1);
		/*
		 * Need to give the charger time (~312 mSec) before the
		 * bc12_type is available. The main task loop will schedule a
		 * task wait event which will then call bd9995x_bc12_get_type.
		 */
		return 1;
	}

	/* Reset BC1.2 regs so we don't do auto-detection. */
	bd9995x_bc12_detect(port, 0);

	/*
	 * VBUS is no longer being provided, if the bc12_type had been
	 * previously determined, then need to detach.
	 */
	if (bc12_detected_type[port] != CHARGE_SUPPLIER_NONE) {
		/* Charger/sink detached */
		bd9995x_bc12_detach(port, bc12_detected_type[port]);
		bc12_detected_type[port] = CHARGE_SUPPLIER_NONE;
	}
	/* No need for the task to schedule a wait event */
	return 0;
}

#ifdef CONFIG_CHARGE_RAMP_SW
int usb_charger_ramp_allowed(int supplier)
{
	return supplier == CHARGE_SUPPLIER_BC12_DCP ||
	       supplier == CHARGE_SUPPLIER_BC12_SDP ||
	       supplier == CHARGE_SUPPLIER_BC12_CDP ||
	       supplier == CHARGE_SUPPLIER_OTHER;
}

int usb_charger_ramp_max(int supplier, int sup_curr)
{
	return bd9995x_get_bc12_ilim(supplier);
}
#endif /* CONFIG_CHARGE_RAMP_SW */
#endif /* HAS_TASK_USB_CHG */

/* chip specific interfaces */

int charger_set_input_current(int input_current)
{
	int rv;

	/* Input current step 32 mA */
	input_current &= ~0x1F;

	if (input_current < bd9995x_charger_info.input_current_min)
		input_current = bd9995x_charger_info.input_current_min;

	rv = ch_raw_write16(BD9995X_CMD_IBUS_LIM_SET, input_current,
				BD9995X_BAT_CHG_COMMAND);
	if (rv)
		return rv;

	return ch_raw_write16(BD9995X_CMD_ICC_LIM_SET, input_current,
				BD9995X_BAT_CHG_COMMAND);
}

int charger_get_input_current(int *input_current)
{
	return ch_raw_read16(BD9995X_CMD_CUR_ILIM_VAL, input_current,
			     BD9995X_EXTENDED_COMMAND);
}

int charger_manufacturer_id(int *id)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_device_id(int *id)
{
	return ch_raw_read16(BD9995X_CMD_CHIP_ID, id, BD9995X_EXTENDED_COMMAND);
}

int charger_get_option(int *option)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD9995X_CMD_CHGOP_SET1, option,
				BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	rv = ch_raw_read16(BD9995X_CMD_CHGOP_SET2, &reg,
				BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	*option |= reg << 16;

	return EC_SUCCESS;
}

int charger_set_option(int option)
{
	int rv;

	rv = ch_raw_write16(BD9995X_CMD_CHGOP_SET1, option & 0xFFFF,
				BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	return ch_raw_write16(BD9995X_CMD_CHGOP_SET2, (option >> 16) & 0xFFFF,
				BD9995X_EXTENDED_COMMAND);
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &bd9995x_charger_info;
}

int charger_get_status(int *status)
{
	int rv;
	int reg;
	int ch_status;

	/* charger level */
	*status = CHARGER_LEVEL_2;

	/* charger enable/inhibit */
	rv = ch_raw_read16(BD9995X_CMD_CHGOP_SET2, &reg,
				BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (!(reg & BD9995X_CMD_CHGOP_SET2_CHG_EN))
		*status |= CHARGER_CHARGE_INHIBITED;

	/* charger alarm enable/inhibit */
	rv = ch_raw_read16(BD9995X_CMD_PROCHOT_CTRL_SET, &reg,
				BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (!(reg & (BD9995X_CMD_PROCHOT_CTRL_SET_PROCHOT_EN4 |
			BD9995X_CMD_PROCHOT_CTRL_SET_PROCHOT_EN3 |
			BD9995X_CMD_PROCHOT_CTRL_SET_PROCHOT_EN2 |
			BD9995X_CMD_PROCHOT_CTRL_SET_PROCHOT_EN1 |
			BD9995X_CMD_PROCHOT_CTRL_SET_PROCHOT_EN0)))
		*status |= CHARGER_ALARM_INHIBITED;

	rv = bd9995x_get_charger_op_status(&reg);
	if (rv)
		return rv;

	/* power fail */
	if (!(reg & BD9995X_CMD_CHGOP_STATUS_RBOOST_UV))
		*status |= CHARGER_POWER_FAIL;

	/* Safety signal ranges & battery presence */
	ch_status = (reg & BD9995X_BATTTEMP_MASK) >> 8;

	*status |= CHARGER_BATTERY_PRESENT;

	switch (ch_status) {
	case BD9995X_CMD_CHGOP_STATUS_BATTEMP_COLD1:
		*status |= CHARGER_RES_COLD;
		break;
	case BD9995X_CMD_CHGOP_STATUS_BATTEMP_COLD2:
		*status |= CHARGER_RES_COLD;
		*status |= CHARGER_RES_UR;
		break;
	case BD9995X_CMD_CHGOP_STATUS_BATTEMP_HOT1:
	case BD9995X_CMD_CHGOP_STATUS_BATTEMP_HOT2:
		*status |= CHARGER_RES_HOT;
		break;
	case BD9995X_CMD_CHGOP_STATUS_BATTEMP_HOT3:
		*status |= CHARGER_RES_HOT;
		*status |= CHARGER_RES_OR;
		break;
	case BD9995X_CMD_CHGOP_STATUS_BATTEMP_BATOPEN:
		*status &= ~CHARGER_BATTERY_PRESENT;
	default:
		break;
	}

	/* source of power */
	if (bd9995x_is_vbus_provided(BD9995X_CHARGE_PORT_BOTH))
		*status |= CHARGER_AC_PRESENT;

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	int rv;

	if (mode & CHARGE_FLAG_POR_RESET) {
		rv = bd9995x_por_reset();
		if (rv)
			return rv;
	}

	if (mode & CHARGE_FLAG_RESET_TO_ZERO) {
		rv = bd9995x_reset_to_zero();
		if (rv)
			return rv;
	}

	return EC_SUCCESS;
}

int charger_get_current(int *current)
{
	return ch_raw_read16(BD9995X_CMD_CHG_CURRENT, current,
				BD9995X_BAT_CHG_COMMAND);
}

int charger_set_current(int current)
{
	int rv;
	int chg_enable = 1;

	/* Charge current step 64 mA */
	current &= ~0x3F;

	if (current < BD9995X_NO_BATTERY_CHARGE_I_MIN &&
	    (battery_is_present() != BP_YES || battery_is_cut_off()))
		current = BD9995X_NO_BATTERY_CHARGE_I_MIN;

	/*
	 * Disable charger before setting charge current to 0 or when
	 * discharging on AC.
	 * If charging current is set to 0mA during charging, reference of
	 * the charge current feedback amp (VREF_CHG) is set to 0V. Hence
	 * the DCDC stops switching (because of the EA offset).
	 */
	if (!current || bd9995x_is_discharging_on_ac()) {
		chg_enable = 0;
		rv = bd9995x_charger_enable(0);
		if (rv)
			return rv;
	}

	rv = ch_raw_write16(BD9995X_CMD_IPRECH_SET,
			    MIN(current, BD9995X_IPRECH_MAX),
			    BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	rv = ch_raw_write16(BD9995X_CMD_CHG_CURRENT, current,
			      BD9995X_BAT_CHG_COMMAND);
	if (rv)
		return rv;

	/*
	 * Enable charger if charge current is non-zero or not discharging
	 * on AC.
	 */
	return chg_enable ? bd9995x_charger_enable(1) : EC_SUCCESS;
}

int charger_get_voltage(int *voltage)
{
	if (vsys_priority) {
		int batt_volt_measured;
		int reg;
		int rv;

		/* Get battery voltage as reported by charger */
		batt_volt_measured = bd9995x_get_battery_voltage();
		if (batt_volt_measured > (battery_get_info()->voltage_min +
					  BD9995X_VSYS_PRECHARGE_OFFSET_MV)) {
			/*
			 * Battery is not deeply discharged. Clear the
			 * VSYS_PRIORITY bit to ensure that input current limit
			 * is always active.
			 */
			mutex_lock(&bd9995x_vin_mutex);
			if (!ch_raw_read16(BD9995X_CMD_VIN_CTRL_SET, &reg,
					   BD9995X_EXTENDED_COMMAND)) {
				reg &= ~BD9995X_CMD_VIN_CTRL_SET_VSYS_PRIORITY;
				rv = ch_raw_write16(BD9995X_CMD_VIN_CTRL_SET,
						    reg,
						    BD9995X_EXTENDED_COMMAND);

				/* Mirror the state of this bit */
				if (!rv)
					vsys_priority = 0;
			}
			mutex_unlock(&bd9995x_vin_mutex);
		}
	}

	return ch_raw_read16(BD9995X_CMD_CHG_VOLTAGE, voltage,
				BD9995X_BAT_CHG_COMMAND);
}

int charger_set_voltage(int voltage)
{
	const int battery_voltage_max = battery_get_info()->voltage_max;

	/*
	 * Regulate the system voltage to battery max if the battery
	 * is not present or the battery is discharging on AC.
	 */
	if (voltage == 0 ||
		bd9995x_is_discharging_on_ac() ||
		battery_is_present() != BP_YES ||
		battery_is_cut_off() ||
		voltage > battery_voltage_max)
		voltage = battery_voltage_max;

	/* Charge voltage step 16 mV */
	voltage &= ~0x0F;

	/* Assumes charger's voltage_min < battery's voltage_max */
	if (voltage < bd9995x_charger_info.voltage_min)
		voltage = bd9995x_charger_info.voltage_min;

	return bd9995x_set_vfastchg(voltage);
}

static void bd9995x_battery_charging_profile_settings(void)
{
	const struct battery_info *bi = battery_get_info();

	/* Input Current Limit Setting */
	charger_set_input_current(CONFIG_CHARGER_INPUT_CURRENT);

	/* Charge Termination Current Setting */
	ch_raw_write16(BD9995X_CMD_ITERM_SET, 0, BD9995X_EXTENDED_COMMAND);

	/* Trickle-charge Current Setting */
	ch_raw_write16(BD9995X_CMD_ITRICH_SET,
		       bi->precharge_current & 0x07C0,
		       BD9995X_EXTENDED_COMMAND);

	bd9995x_set_vfastchg(bi->voltage_max);

	/* Set Pre-charge Voltage Threshold for trickle charging. */
	ch_raw_write16(BD9995X_CMD_VPRECHG_TH_SET,
		       (bi->voltage_min - 1000) & 0x7FC0,
		       BD9995X_EXTENDED_COMMAND);

	/* Re-charge Battery Voltage Setting */
	ch_raw_write16(BD9995X_CMD_VRECHG_SET,
		       bi->voltage_max & 0x7FF0,
		       BD9995X_EXTENDED_COMMAND);

	/* Set battery OVP to 500 + maximum battery voltage */
	ch_raw_write16(BD9995X_CMD_VBATOVP_SET,
		       (bi->voltage_max + 500) & 0x7ff0,
		       BD9995X_EXTENDED_COMMAND);

	/* Reverse buck boost voltage Setting */
	ch_raw_write16(BD9995X_CMD_VRBOOST_SET, 0,
		       BD9995X_EXTENDED_COMMAND);

	/* Disable fast/pre-charging watchdog */
	ch_raw_write16(BD9995X_CMD_CHGWDT_SET, 0,
		       BD9995X_EXTENDED_COMMAND);

	/* TODO(crosbug.com/p/55626): Set  VSYSVAL_THH/THL appropriately */
}

static void bd9995x_init(void)
{
	int reg;

	/*
	 * Disable charging trigger by BC1.2 on VCC & VBUS and
	 * automatic limitation of the input current.
	 */
	if (ch_raw_read16(BD9995X_CMD_CHGOP_SET1, &reg,
			  BD9995X_EXTENDED_COMMAND))
		return;
	reg |= (BD9995X_CMD_CHGOP_SET1_SDP_CHG_TRIG_EN |
		BD9995X_CMD_CHGOP_SET1_SDP_CHG_TRIG |
		BD9995X_CMD_CHGOP_SET1_VBUS_BC_DISEN |
		BD9995X_CMD_CHGOP_SET1_VCC_BC_DISEN |
		BD9995X_CMD_CHGOP_SET1_ILIM_AUTO_DISEN |
		BD9995X_CMD_CHGOP_SET1_SDP_500_SEL |
		BD9995X_CMD_CHGOP_SET1_DCP_2500_SEL);
	ch_raw_write16(BD9995X_CMD_CHGOP_SET1, reg,
		       BD9995X_EXTENDED_COMMAND);

	/*
	 * OTP setting for this register is 6.08V. Set VSYS to above battery max
	 * (as is done when charger is disabled) to ensure VSYSREG_SET > VBAT so
	 * that the charger is in Pre-Charge state and that the input current
	 * disable setting below will be active.
	 */
	bd9995x_set_vsysreg(battery_get_info()->voltage_max +
			    BD9995X_VSYS_PRECHARGE_OFFSET_MV);

	/* Enable BC1.2 USB charging and DC/DC converter @ 1200KHz */
	if (ch_raw_read16(BD9995X_CMD_CHGOP_SET2, &reg,
			  BD9995X_EXTENDED_COMMAND))
		return;
	reg &= ~(BD9995X_CMD_CHGOP_SET2_USB_SUS |
		 BD9995X_CMD_CHGOP_SET2_DCDC_CLK_SEL);
	reg |= BD9995X_CMD_CHGOP_SET2_DCDC_CLK_SEL_1200;
#ifdef CONFIG_CHARGER_BD9995X_CHGEN
	reg |= BD9995X_CMD_CHGOP_SET2_CHG_EN;
#endif
	ch_raw_write16(BD9995X_CMD_CHGOP_SET2, reg,
		       BD9995X_EXTENDED_COMMAND);

	/*
	 * We disable IADP (here before setting IBUS_LIM_SET and ICC_LIM_SET)
	 * to prevent voltage on IADP/RESET pin from affecting SEL_ILIM_VAL.
	 */
	if (ch_raw_read16(BD9995X_CMD_VM_CTRL_SET, &reg,
			  BD9995X_EXTENDED_COMMAND))
		return;
	reg &= ~BD9995X_CMD_VM_CTRL_SET_EXTIADPEN;
	ch_raw_write16(BD9995X_CMD_VM_CTRL_SET, reg, BD9995X_EXTENDED_COMMAND);
	/*
	 * Disable the input current limit when VBAT is < VSYSREG_SET. This
	 * needs to be done before calling
	 * bd9995x_battery_charging_profile_settings() as in that function the
	 * input current limit is set to CONFIG_CHARGER_INPUT_CURRENT which is
	 * 512 mA. In deeply discharged battery cases, setting the input current
	 * limit this low can cause VSYS to collapse, which in turn can cause
	 * the EC's brownout detector to reset the EC.
	 */
	if (ch_raw_read16(BD9995X_CMD_VIN_CTRL_SET, &reg,
			  BD9995X_EXTENDED_COMMAND))
		return;
	reg |= BD9995X_CMD_VIN_CTRL_SET_VSYS_PRIORITY;
	ch_raw_write16(BD9995X_CMD_VIN_CTRL_SET, reg,
		       BD9995X_EXTENDED_COMMAND);
	/* Mirror the state of this bit */
	vsys_priority = 1;

	/* Define battery charging profile */
	bd9995x_battery_charging_profile_settings();

	/* Power save mode when VBUS/VCC is removed. */
#ifdef CONFIG_BD9995X_POWER_SAVE_MODE
	bd9995x_set_power_save_mode(CONFIG_BD9995X_POWER_SAVE_MODE);
#else
	bd9995x_set_power_save_mode(BD9995X_PWR_SAVE_OFF);
#endif

#ifdef CONFIG_USB_PD_DISCHARGE
	/* Set VBUS / VCC detection threshold for discharge enable */
	ch_raw_write16(BD9995X_CMD_VBUS_TH_SET, BD9995X_VBUS_DISCHARGE_TH,
		       BD9995X_EXTENDED_COMMAND);
	ch_raw_write16(BD9995X_CMD_VCC_TH_SET, BD9995X_VBUS_DISCHARGE_TH,
		       BD9995X_EXTENDED_COMMAND);
#endif

	/* Unlock debug regs */
	ch_raw_write16(BD9995X_CMD_PROTECT_SET, 0x3c, BD9995X_EXTENDED_COMMAND);

	/* Undocumented - reverse current threshold = -50mV */
	ch_raw_write16(0x14, 0x0202, BD9995X_DEBUG_COMMAND);
	/* Undocumented - internal gain = 2x */
	ch_raw_write16(0x1a, 0x80, BD9995X_DEBUG_COMMAND);

	/* Re-lock debug regs */
	ch_raw_write16(BD9995X_CMD_PROTECT_SET, 0x0, BD9995X_EXTENDED_COMMAND);
}
DECLARE_HOOK(HOOK_INIT, bd9995x_init, HOOK_PRIO_INIT_EXTPOWER);

int charger_post_init(void)
{
	return EC_SUCCESS;
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD9995X_CMD_CHGOP_SET2, &reg,
				BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/*
	 * Suspend USB charging and DC/DC converter so that BATT_LEARN mode
	 * doesn't auto exit if VBAT < VSYSVAL_THL_SET and also it helps to
	 * discharge VBUS quickly when charging is not allowed and the AC
	 * is removed.
	 */
	if (enable)
		reg |= BD9995X_CMD_CHGOP_SET2_BATT_LEARN |
			BD9995X_CMD_CHGOP_SET2_USB_SUS;
	else
		reg &= ~(BD9995X_CMD_CHGOP_SET2_BATT_LEARN |
			BD9995X_CMD_CHGOP_SET2_USB_SUS);

	return ch_raw_write16(BD9995X_CMD_CHGOP_SET2, reg,
				BD9995X_EXTENDED_COMMAND);
}

int charger_get_vbus_voltage(int port)
{
	uint8_t read_reg;
	int voltage;

	read_reg = (port == BD9995X_CHARGE_PORT_VBUS) ? BD9995X_CMD_VBUS_VAL :
							BD9995X_CMD_VCC_VAL;

	return ch_raw_read16(read_reg, &voltage, BD9995X_EXTENDED_COMMAND) ?
			     0 : voltage;
}

/*** Non-standard interface functions ***/

int bd9995x_is_vbus_provided(enum bd9995x_charge_port port)
{
	int reg;

	if (ch_raw_read16(BD9995X_CMD_VBUS_VCC_STATUS, &reg,
			  BD9995X_EXTENDED_COMMAND))
		return 0;

	if (port == BD9995X_CHARGE_PORT_VBUS)
		reg &= BD9995X_CMD_VBUS_VCC_STATUS_VBUS_DETECT;
	else if (port == BD9995X_CHARGE_PORT_VCC)
		reg &= BD9995X_CMD_VBUS_VCC_STATUS_VCC_DETECT;
	else if (port == BD9995X_CHARGE_PORT_BOTH) {
		/* Check VBUS on either port */
		reg &= (BD9995X_CMD_VBUS_VCC_STATUS_VCC_DETECT |
			BD9995X_CMD_VBUS_VCC_STATUS_VBUS_DETECT);
	} else
		reg = 0;

	return !!reg;
}

#ifdef CONFIG_BD9995X_DELAY_INPUT_PORT_SELECT
static int bd9995x_select_input_port_private(enum bd9995x_charge_port port,
								int select)
#else
int bd9995x_select_input_port(enum bd9995x_charge_port port, int select)
#endif
{
	int rv;
	int reg;

	mutex_lock(&bd9995x_vin_mutex);
	rv = ch_raw_read16(BD9995X_CMD_VIN_CTRL_SET, &reg,
			   BD9995X_EXTENDED_COMMAND);
	if (rv)
		goto select_input_port_exit;

	if (select) {
		if (port == BD9995X_CHARGE_PORT_VBUS) {
			reg |= BD9995X_CMD_VIN_CTRL_SET_VBUS_EN;
			reg &= ~BD9995X_CMD_VIN_CTRL_SET_VCC_EN;
		} else if (port == BD9995X_CHARGE_PORT_VCC) {
			reg |= BD9995X_CMD_VIN_CTRL_SET_VCC_EN;
			reg &= ~BD9995X_CMD_VIN_CTRL_SET_VBUS_EN;
		} else if (port == BD9995X_CHARGE_PORT_BOTH) {
			/* Enable both the ports for PG3 */
			reg |= BD9995X_CMD_VIN_CTRL_SET_VBUS_EN |
				BD9995X_CMD_VIN_CTRL_SET_VCC_EN;
		} else {
			/* Invalid charge port */
			panic("Invalid charge port");
		}
	} else {
		if (port == BD9995X_CHARGE_PORT_VBUS)
			reg &= ~BD9995X_CMD_VIN_CTRL_SET_VBUS_EN;
		else if (port == BD9995X_CHARGE_PORT_VCC)
			reg &= ~BD9995X_CMD_VIN_CTRL_SET_VCC_EN;
		else if (port == BD9995X_CHARGE_PORT_BOTH)
			reg &= ~(BD9995X_CMD_VIN_CTRL_SET_VBUS_EN |
				 BD9995X_CMD_VIN_CTRL_SET_VCC_EN);
		else
			panic("Invalid charge port");
	}

	rv = ch_raw_write16(BD9995X_CMD_VIN_CTRL_SET, reg,
			      BD9995X_EXTENDED_COMMAND);
select_input_port_exit:
	mutex_unlock(&bd9995x_vin_mutex);
	return rv;
}

#ifdef CONFIG_BD9995X_DELAY_INPUT_PORT_SELECT
int bd9995x_select_input_port(enum bd9995x_charge_port port, int select)
{
	port_update = port;
	select_update = select;
	vbus_state = START;
	select_input_port_update = 1;
	task_wake(TASK_ID_USB_CHG);

	return EC_SUCCESS;
}

static inline int bd9995x_vbus_test(int value, int limit)
{
	uint32_t hi_value = limit + VBUS_DELTA;
	uint32_t lo_value = limit - VBUS_DELTA;

	return ((value > lo_value) && (value < hi_value));
}

static int bd9995x_vbus_debounce(enum bd9995x_charge_port port)
{
	int vbus_reg;
	int voltage;

	vbus_reg = (port == BD9995X_CHARGE_PORT_VBUS) ?
		BD9995X_CMD_VBUS_VAL : BD9995X_CMD_VCC_VAL;
	if (ch_raw_read16(vbus_reg, &voltage, BD9995X_EXTENDED_COMMAND))
		voltage = 0;

	if (!bd9995x_vbus_test(voltage, vbus_voltage)) {
		vbus_voltage = voltage;
		debounce_time = get_time().val + VBUS_MSEC;
	} else {
		if (get_time().val >= debounce_time)
			return 1;
	}

	return 0;
}
#endif


#ifdef CONFIG_CHARGER_BATTERY_TSENSE
int bd9995x_get_battery_temp(int *temp_ptr)
{
	int rv;

	rv = ch_raw_read16(BD9995X_CMD_THERM_VAL, temp_ptr,
			BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Degrees C = 200 - THERM_VAL, range is -55C-200C, 1C steps */
	*temp_ptr = 200 - *temp_ptr;
	return EC_SUCCESS;
}
#endif

void bd9995x_set_power_save_mode(int mode)
{
	ch_raw_write16(BD9995X_CMD_SMBREG, mode, BD9995X_EXTENDED_COMMAND);
}

int bd9995x_get_battery_voltage(void)
{
	int vbat_val, rv;

	rv = ch_raw_read16(BD9995X_CMD_VBAT_VAL, &vbat_val,
			BD9995X_EXTENDED_COMMAND);

	return rv ? 0 : vbat_val;
}

#ifdef HAS_TASK_USB_CHG
int bd9995x_bc12_enable_charging(int port, int enable)
{
	int rv;
	int reg;
	int mask_val;

	/*
	 * For BC1.2, enable VBUS/VCC_BC_DISEN charging trigger by BC1.2
	 * detection and disable SDP_CHG_TRIG, SDP_CHG_TRIG_EN. Vice versa
	 * for USB-C.
	 */
	rv = ch_raw_read16(BD9995X_CMD_CHGOP_SET1, &reg,
			BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	mask_val = (BD9995X_CMD_CHGOP_SET1_SDP_CHG_TRIG_EN |
		BD9995X_CMD_CHGOP_SET1_SDP_CHG_TRIG |
		((port == BD9995X_CHARGE_PORT_VBUS) ?
		BD9995X_CMD_CHGOP_SET1_VBUS_BC_DISEN :
		BD9995X_CMD_CHGOP_SET1_VCC_BC_DISEN));

	if (enable)
		reg &= ~mask_val;
	else
		reg |= mask_val;

	return ch_raw_write16(BD9995X_CMD_CHGOP_SET1, reg,
			BD9995X_EXTENDED_COMMAND);
}

void usb_charger_set_switches(int port, enum usb_switch setting)
{
	/* If switch is not changing then return */
	if (setting == usb_switch_state[port])
		return;

	if (setting != USB_SWITCH_RESTORE)
		usb_switch_state[port] = setting;

	/* ensure we disable power saving when we are using DP/DN */
#ifdef CONFIG_BD9995X_POWER_SAVE_MODE
	bd9995x_set_power_save_mode(
		(usb_switch_state[0] == USB_SWITCH_DISCONNECT &&
		usb_switch_state[1] == USB_SWITCH_DISCONNECT)
		? CONFIG_BD9995X_POWER_SAVE_MODE : BD9995X_PWR_SAVE_OFF);
#endif

	bd9995x_update_ucd_set_reg(port, BD9995X_CMD_UCD_SET_USB_SW,
		usb_switch_state[port] == USB_SWITCH_CONNECT);
}

void bd9995x_vbus_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_USB_CHG);
}

void usb_charger_task(void *u)
{
	static int initialized;
	int changed, port, interrupts;
	int sleep_usec;
	uint64_t bc12_det_mark[CONFIG_USB_PD_PORT_MAX_COUNT];
#ifdef CONFIG_USB_PD_DISCHARGE
	int vbus_reg, voltage;
#endif

#ifdef CONFIG_BD9995X_DELAY_INPUT_PORT_SELECT
	select_input_port_update = 0;
	vbus_voltage = 0;
#endif

	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		bc12_detected_type[port] = CHARGE_SUPPLIER_NONE;
		bd9995x_enable_vbus_detect_interrupts(port, 1);
		bc12_det_mark[port] = 0;
	}

	while (1) {
		sleep_usec = -1;
		changed = 0;
		for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
			/* Get port interrupts */
			interrupts = bd9995x_get_interrupts(port);
			if (interrupts & BD9995X_CMD_INT_VBUS_DET ||
			    !initialized) {
				/*
				 * Detect based on current state of VBUS. If
				 * VBUS is provided, then need to wait for
				 * bc12_type to be available. If VBUS is not
				 * provided, then disable wait for this port.
				 */
				bc12_det_mark[port] = usb_charger_process(port)
					? get_time().val + BC12_DETECT_US : 0;
				changed = 1;
			}
#ifdef CONFIG_USB_PD_DISCHARGE
			if (interrupts & BD9995X_CMD_INT_VBUS_TH ||
			    !initialized) {
				/* Get VBUS voltage */
				vbus_reg = (port == BD9995X_CHARGE_PORT_VBUS) ?
					   BD9995X_CMD_VBUS_VAL :
					   BD9995X_CMD_VCC_VAL;
				if (ch_raw_read16(vbus_reg,
						  &voltage,
						  BD9995X_EXTENDED_COMMAND))
					voltage = 0;

				/* Set discharge accordingly */
				pd_set_vbus_discharge(port,
					voltage < BD9995X_VBUS_DISCHARGE_TH);
				changed = 1;
			}
#endif
			if (bc12_det_mark[port] && (get_time().val >
						    bc12_det_mark[port])) {
				/*
				 * bc12_type result should be available. If not
				 * available still, then function will return
				 * 1. Set up additional 100 msec wait. Note that
				 * if VBUS is no longer provided when this call
				 * happens the funciton will return 0.
				 */
				bc12_det_mark[port] =
					bd9995x_bc12_check_type(port) ?
					get_time().val + 100 * MSEC : 0;
				/* Reset BC1.2 regs to skip auto-detection. */
				bd9995x_bc12_detect(port, 0);
			}

			/*
			 * Determine if a wait for reading bc12_type needs to be
			 * scheduled. Use the scheduled wait for this port if
			 * it's less than the wait needed for a previous
			 * port. If previous port(s) don't need a wait, then
			 * sleep_usec will be -1.
			 */
			if (bc12_det_mark[port]) {
				int bc12_wait_usec;

				bc12_wait_usec = bc12_det_mark[port]
					- get_time().val;
				if ((sleep_usec < 0) ||
				    (sleep_usec > bc12_wait_usec))
					sleep_usec = bc12_wait_usec;
			}
		}

		initialized = 1;
#ifdef CONFIG_BD9995X_DELAY_INPUT_PORT_SELECT
/*
 * When a charge port is selected and VBUS is 5V, the inrush current on some
 * devices causes VBUS to droop, which could signal a sink disconnection.
 *
 * To mitigate the problem, charge port selection is delayed until VBUS
 * is stable or one second has passed. Hopefully PD has negotiated a VBUS
 * voltage of at least 9V before the one second timeout.
 */
	if (select_input_port_update) {
		sleep_usec = VBUS_CHECK_MSEC;
		changed = 0;

		switch (vbus_state) {
		case START:
			vbus_timeout = get_time().val + STABLE_TIMEOUT;
			vbus_state = STABLE;
			break;
		case STABLE:
			if (get_time().val > vbus_timeout) {
				vbus_state = DEBOUNCE;
				vbus_timeout = get_time().val +
							DEBOUNCE_TIMEOUT;
			}
			break;
		case DEBOUNCE:
			if (bd9995x_vbus_debounce(port_update) ||
					get_time().val > vbus_timeout) {
				select_input_port_update = 0;
				bd9995x_select_input_port_private(
						port_update, select_update);
			}
			break;
		}
	}
#endif

		/*
		 * Re-read interrupt registers immediately if we got an
		 * interrupt. We're dealing with multiple independent
		 * interrupt sources and the interrupt pin may have
		 * never deasserted if both sources were not in clear
		 * state simultaneously.
		 */
		if (!changed)
			task_wait_event(sleep_usec);
	}
}
#endif /* HAS_TASK_USB_CHG */


/*** Console commands ***/
#ifdef CONFIG_CMD_CHARGER_DUMP
static int read_bat(uint8_t cmd)
{
	int read = 0;

	ch_raw_read16(cmd, &read, BD9995X_BAT_CHG_COMMAND);
	return read;
}

static int read_ext(uint8_t cmd)
{
	int read = 0;

	ch_raw_read16(cmd, &read, BD9995X_EXTENDED_COMMAND);
	return read;
}

/* Dump all readable registers on bd9995x */
static int console_bd9995x_dump_regs(int argc, char **argv)
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
DECLARE_CONSOLE_COMMAND(charger_dump, console_bd9995x_dump_regs,
			NULL,
			"Dump all charger registers");
#endif /* CONFIG_CMD_CHARGER_DUMP */

#ifdef CONFIG_CMD_CHARGER
static int console_command_bd9995x(int argc, char **argv)
{
	int rv, reg, data, val;
	char rw, *e;
	enum bd9995x_command cmd;

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
DECLARE_CONSOLE_COMMAND(bd9995x, console_command_bd9995x,
			"bd9995x <r/w> <reg_hex> <cmd_type> | <val_hex>",
			"Read or write a charger register");
#endif /* CONFIG_CMD_CHARGER */

#ifdef CONFIG_CHARGER_PSYS_READ
static int bd9995x_psys_charger_adc(void)
{
	int i;
	int reg;
	uint64_t ipmon = 0;

	for (i = 0; i < BD9995X_PMON_IOUT_ADC_READ_COUNT; i++) {
		if (ch_raw_read16(BD9995X_CMD_PMON_DACIN_VAL, &reg,
				BD9995X_EXTENDED_COMMAND))
			return 0;

		/* Conversion Interval is 200us */
		usleep(200);
		ipmon += reg;
	}

	/*
	 * Calculate power in mW
	 * PSYS = VACP×IACP+VBAT×IBAT = IPMON / GPMON
	 */
	return (int) ((ipmon * 1000) / (BIT(BD9995X_PSYS_GAIN_SELECT) *
		BD9995X_PMON_IOUT_ADC_READ_COUNT));
}

static int bd9995x_enable_psys(void)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD9995X_CMD_PMON_IOUT_CTRL_SET, &reg,
			BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Enable PSYS & Select PSYS Gain */
	reg &= ~BD9995X_CMD_PMON_IOUT_CTRL_SET_PMON_GAIN_SET_MASK;
	reg |= (BD9995X_CMD_PMON_IOUT_CTRL_SET_PMON_INSEL |
		BD9995X_CMD_PMON_IOUT_CTRL_SET_PMON_OUT_EN |
		BD9995X_PSYS_GAIN_SELECT);

	return ch_raw_write16(BD9995X_CMD_PMON_IOUT_CTRL_SET, reg,
			BD9995X_EXTENDED_COMMAND);
}

/**
 * Get system power.
 *
 * TODO(b:71520677): Implement charger_get_system_power, disable psys readout
 * when not needed (the code below leaves it enabled after the first access),
 * update "psys" console command to use charger_get_system_power and move it
 * to some common code.
 */
static int console_command_psys(int argc, char **argv)
{
	int rv;

	rv = bd9995x_enable_psys();
	if (rv)
		return rv;

	CPRINTS("PSYS from chg_adc: %d mW",
			bd9995x_psys_charger_adc());

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(psys, console_command_psys,
			NULL,
			"Get the system power in mW");
#endif /* CONFIG_CHARGER_PSYS_READ */

#ifdef CONFIG_CMD_CHARGER_ADC_AMON_BMON
static int bd9995x_amon_bmon_chg_adc(void)
{
	int i;
	int reg;
	int iout = 0;

	for (i = 0; i < BD9995X_PMON_IOUT_ADC_READ_COUNT; i++) {
		ch_raw_read16(BD9995X_CMD_IOUT_DACIN_VAL, &reg,
				BD9995X_EXTENDED_COMMAND);
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
	return (iout * (5 << BD9995X_IOUT_GAIN_SELECT)) /
		(10 * BD9995X_PMON_IOUT_ADC_READ_COUNT);
}

static int bd9995x_amon_bmon(int amon_bmon)
{
	int rv;
	int reg;
	int imon;
	int sns_res;

	rv = ch_raw_read16(BD9995X_CMD_PMON_IOUT_CTRL_SET, &reg,
			BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	/* Enable monitor */
	reg &= ~BD9995X_CMD_PMON_IOUT_CTRL_SET_IOUT_GAIN_SET_MASK;
	reg |= (BD9995X_CMD_PMON_IOUT_CTRL_SET_IMON_INSEL |
		BD9995X_CMD_PMON_IOUT_CTRL_SET_IOUT_OUT_EN |
		(BD9995X_IOUT_GAIN_SELECT << 4));

	if (amon_bmon) {
		reg |= BD9995X_CMD_PMON_IOUT_CTRL_SET_IOUT_SOURCE_SEL;
		sns_res = CONFIG_CHARGER_SENSE_RESISTOR_AC;
	} else {
		reg &= ~BD9995X_CMD_PMON_IOUT_CTRL_SET_IOUT_SOURCE_SEL;
		sns_res = CONFIG_CHARGER_SENSE_RESISTOR;
	}

	rv = ch_raw_write16(BD9995X_CMD_PMON_IOUT_CTRL_SET, reg,
			BD9995X_EXTENDED_COMMAND);
	if (rv)
		return rv;

	imon = bd9995x_amon_bmon_chg_adc();

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
		rv = bd9995x_amon_bmon(1);

	/* Switch to BMON */
	if (argc == 1 || (argc >= 2 && argv[1][0] == 'b'))
		rv = bd9995x_amon_bmon(0);

	return rv;
}
DECLARE_CONSOLE_COMMAND(amonbmon, console_command_amon_bmon,
			"amonbmon [a|b]",
			"Get charger AMON/BMON voltage diff, current");
#endif /* CONFIG_CMD_CHARGER_ADC_AMON_BMON */

#ifdef CONFIG_CMD_I2C_STRESS_TEST_CHARGER
static int bd9995x_i2c_read(const int reg, int *data)
{
	return ch_raw_read16(reg, data, BD9995X_EXTENDED_COMMAND);
}

static int bd9995x_i2c_write(const int reg, int data)
{
	return ch_raw_write16(reg, data, BD9995X_EXTENDED_COMMAND);
}

/* BD9995X_CMD_CHIP_ID register value may vary by chip. */
struct i2c_stress_test_dev bd9995x_i2c_stress_test_dev = {
	.reg_info = {
		.read_reg = BD9995X_CMD_CHIP_ID,
		.read_val = BD99956_CHIP_ID,
		.write_reg = BD9995X_CMD_ITRICH_SET,
	},
	.i2c_read_dev = &bd9995x_i2c_read,
	.i2c_write_dev = &bd9995x_i2c_write,
};
#endif /* CONFIG_CMD_I2C_STRESS_TEST_CHARGER */
