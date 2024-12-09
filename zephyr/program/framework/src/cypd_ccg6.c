#include <atomic.h>
#include <zephyr/init.h>
#include "gpio/gpio_int.h"

#include "battery.h"
#include "board_function.h"
#include "board_host_command.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "common_cpu_power.h"
#include "driver/charger/isl9241.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "power.h"
#include "task.h"
#include "ucsi.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_emsg.h"
#include "usb_tc_sm.h"
#include "util.h"
#include "throttle_ap.h"
#include "zephyr_console_shim.h"

#ifdef CONFIG_BOARD_MARIGOLD
#include "marigold/charger.h"
#endif

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)


int cypd_write_reg8_wait_ack(int controller, int reg, int data)
{
	int rv = EC_SUCCESS;
	int intr_status;
	int event;
	int cmd_port = -1;
	int ack_mask = 0;
	int expected_ack_mask = 0;
	const struct gpio_dt_spec *intr = gpio_get_dt_spec(pd_chip_config[controller].gpio);

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (reg < 0x1000) {
		expected_ack_mask = CCG_DEV_INTR;
		cmd_port = -1;
	} else if (reg < 0x2000) {
		expected_ack_mask = CCG_PORT0_INTR;
		cmd_port = 0;
	} else {
		expected_ack_mask = CCG_PORT1_INTR;
		cmd_port = 1;
	}

	if (gpio_pin_get_dt(intr) == 0) {
		/* we may have a pending interrupt */
		rv = cypd_get_int(controller, &intr_status);
		CPRINTS("%s pre 0x%x ", __func__, intr_status);
		if (intr_status & CCG_DEV_INTR) {
			rv = cypd_read_reg16(controller, CCG_RESPONSE_REG, &event);
			if (event < 0x80) {
				cypd_clear_int(controller, CCG_DEV_INTR);
			}
			crec_usleep(50);
		}
	}


	rv = cypd_write_reg8(controller, reg, data);
	if (rv != EC_SUCCESS) {
		CPRINTS("Write Reg8 0x%x fail!", reg);
		return EC_ERROR_INVAL;
	}

	if (cypd_wait_for_ack(controller, 100) != EC_SUCCESS) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	rv = cypd_get_int(controller, &intr_status);
	if (rv != EC_SUCCESS)
		CPRINTS("Get INT Fail");

	if (intr_status & CCG_DEV_INTR && cmd_port == -1) {
		/* read the response register */
		rv = cypd_read_reg16(controller, CCG_RESPONSE_REG, &event);
		if (rv != EC_SUCCESS)
			CPRINTS("fail to read DEV response");

		/* check the ac port */
		if (data == CCG6_AC_AT_PORT) {
			switch (event) {
			case CCG6_RESPONSE_AC_AT_P0:
				pd_port_states[(controller * 2) + 0].ac_port = 1;
				event = CCG_RESPONSE_SUCCESS;
				break;
			case CCG6_RESPONSE_AC_AT_P1:
				pd_port_states[(controller * 2) + 1].ac_port = 1;
				event = CCG_RESPONSE_SUCCESS;
				break;
			case CCG6_RESPONSE_NO_AC:
			case CCG6_RESPONSE_EC_MODE:
				event = CCG_RESPONSE_SUCCESS;
				break;
			default:
				CPRINTS("Check AC get unknown event 0x%04x", event);
			}
		}

		ack_mask = CCG_DEV_INTR;
	} else if (intr_status & CCG_PORT0_INTR && cmd_port == 0) {
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(0), &event);
		if (rv != EC_SUCCESS)
			CPRINTS("fail to read P0 response");
		ack_mask = CCG_PORT0_INTR;
	} else if (intr_status & CCG_PORT1_INTR && cmd_port == 1) {
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(1), &event);
		if (rv != EC_SUCCESS)
			CPRINTS("fail to read P1 response");
		ack_mask = CCG_PORT1_INTR;
	} else {
		CPRINTS("%s C:%d Unexpected response 0x%x to reg 0x%x",
			__func__, controller, intr_status, reg);
		rv = cypd_read_reg16(controller, CCG_RESPONSE_REG, &event);
		CPRINTS("Dev 0x%x", event);
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(0), &event);
		CPRINTS("P0 0x%x", event);
		rv = cypd_read_reg16(controller, CCG_PORT_PD_RESPONSE_REG(1), &event);
		CPRINTS("P1 0x%x", event);
	}

	/* only clear response code let main task handle event code */
	if (event < 0x80) {
		cypd_clear_int(controller, ack_mask);
		if (event != CCG_RESPONSE_SUCCESS) {
			CPRINTS("%s C:%d 0x%x response 0x%x",
				__func__, controller, reg, event);
		}
		rv = (event == CCG_RESPONSE_SUCCESS) ? EC_SUCCESS : EC_ERROR_INVAL;
	}

	crec_usleep(50);
	return rv;
}

/*****************************************************************
 * CCG6 Setup Functions
 ****************************************************************/

#define CYPD_SETUP_CMDS_LEN 4
int cypd_setup(int controller)
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
		/* Set the port event mask */
		/* Set the port 0 PDO 1.5A */
		{ CCG_PD_CONTROL_REG(0), CCG_PD_CMD_SET_TYPEC_1_5A, CCG_PORT0_INTR},
		/* Set the port 1 PDO 1.5A */
		{ CCG_PD_CONTROL_REG(1), CCG_PD_CMD_SET_TYPEC_1_5A, CCG_PORT1_INTR},
		{ CCG_EVENT_MASK_REG(0), 0x27ffff, 4, CCG_PORT0_INTR},
		{ CCG_EVENT_MASK_REG(1), 0x27ffff, 4, CCG_PORT1_INTR },
	};
	BUILD_ASSERT(ARRAY_SIZE(cypd_setup_cmds) == CYPD_SETUP_CMDS_LEN);

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

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
		if (cypd_wait_for_ack(controller, 5) != EC_SUCCESS) {
			CPRINTS("%s timeout on interrupt", __func__);
			return EC_ERROR_INVAL;
		}

		/* clear cmd ack */
		cypd_clear_int(controller, cypd_setup_cmds[i].status_reg);
	}

	/*Notify the PD controller we are done and it can continue init*/
	rv = cypd_write_reg8_wait_ack(controller,
								CCG_PD_CONTROL_REG(0),
								CCG_PD_CMD_EC_INIT_COMPLETE);
	return EC_SUCCESS;
}

void cypd_update_ac_status(int controller)
{
	CPRINTS("Check C%d AC status!", controller);

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (cypd_write_reg8_wait_ack(controller,
		CCG_CUST_C_CTRL_CONTROL_REG, CCG6_AC_AT_PORT))
		CPRINTS("CYPD Read AC status fail");
}

__maybe_unused static bool are_cypd_chips_ready(void)
{
	int controller;

	for (controller = 0; controller < PD_CHIP_COUNT; controller++) {
		if (pd_chip_config[controller].state != CCG_STATE_READY)
			return EC_ERROR_BUSY;
	}

	return EC_SUCCESS;
}

__override void cypd_customize_app_setup(int controller)
{
	/* After cypd setup complete, check the AC status */
	cypd_update_ac_status(controller);
}

/*****************************************************************
 * Charger Port C-FET control Functions
 ****************************************************************/
#ifndef CONFIG_PD_COMMON_VBUS_CONTROL
DECLARE_DEFERRED(update_power_state_deferred);

int check_power_on_port(void)
{
	int port;
	/* only read CYPD when it ready */
	if (!are_cypd_chips_ready()) {
		CPRINTS("CYPD not ready, just delay 100ms to wait");
		crec_msleep(100);
	}

	for (port = 0; port < PD_PORT_COUNT; port++)
		if (pd_port_states[port].ac_port == 1)
			return port;

	/* if no ac port is checked return -1 */
	return -1;
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
	int next_charge_port = get_active_charge_pd_port();
	bool battery_can_discharge = (battery_is_present() == BP_YES) &
		battery_get_disconnect_state();

	/* if no battery, EC should not control C_CTRL */
	if (!battery_can_discharge) {
		/* check if CYPD ready */
		if (charge_port == -1)
			return EC_ERROR_TRY_AGAIN;

		/* store current port and update power limit */
		update_active_charge_pd_port(charge_port);
		hook_call_deferred(&update_power_state_deferred_data, 100 * MSEC);
		return EC_SUCCESS;
	}

	/* port need change, stop all power and ready to switch. */
	if (next_charge_port != -1 && next_charge_port != charge_port) {
		CPRINTS("Disable all type-c port to change the charger port");
		cypd_write_reg8(0, CCG_CUST_C_CTRL_CONTROL_REG, CCG_P0P1_TURN_OFF_C_CTRL);
		cypd_write_reg8(1, CCG_CUST_C_CTRL_CONTROL_REG, CCG_P0P1_TURN_OFF_C_CTRL);
		crec_msleep(250);
	}

	update_active_charge_pd_port(charge_port);

	/* turn on VBUS C-FET of chosen port */
	if (charge_port >= 0) {
		int pd_controller = (charge_port & 0x02) >> 1;
		int pd_port = charge_port & 0x01;

		cypd_write_reg8((~pd_controller) & 0x01, CCG_CUST_C_CTRL_CONTROL_REG,
			CCG_P0P1_TURN_OFF_C_CTRL);
		cypd_write_reg8(pd_controller, CCG_CUST_C_CTRL_CONTROL_REG,
			pd_port ? CCG_P0_OFF_P1_CY : CCG_P0_CY_P1_OFF);
	} else {
		cypd_write_reg8(0, CCG_CUST_C_CTRL_CONTROL_REG, CCG_P0P1_TURN_OFF_C_CTRL);
		cypd_write_reg8(1, CCG_CUST_C_CTRL_CONTROL_REG, CCG_P0P1_TURN_OFF_C_CTRL);
	}

	hook_call_deferred(&update_power_state_deferred_data, 100 * MSEC);

#ifdef CONFIG_BOARD_MARIGOLD
	acok_control(pd_port_states[charge_port].voltage, charge_port);
#endif	/*CONFIG_BOARD_MARIGOLD*/

	return EC_SUCCESS;
}
#endif /* CONFIG_PD_COMMON_VBUS_CONTROL */

void update_system_power_state(int controller)
{
	enum power_state ps = power_get_state();
	/* CCG6 does not support power state G3, just for initial state */
	static uint8_t pre_state = CCG_POWERSTATE_G3;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	switch (ps) {
	case POWER_G3:
	case POWER_S5G3:
	case POWER_S5:
	case POWER_S3S5:
	case POWER_S4S5:
		/* Do not update the same state again */
		if (pre_state != CCG_POWERSTATE_S5)
			cypd_set_power_state(CCG_POWERSTATE_S5, controller);
		pre_state = CCG_POWERSTATE_S5;
		break;
	case POWER_S3:
	case POWER_S4S3:
	case POWER_S5S3:
	case POWER_S0S3:
		/* Do not update the same state again */
		if (pre_state != CCG_POWERSTATE_S3)
			cypd_set_power_state(CCG_POWERSTATE_S3, controller);
		pre_state = CCG_POWERSTATE_S3;
		break;
	case POWER_S0:
	case POWER_S3S0:
	case POWER_S0ixS0: /* S0ix -> S0 */
		if (pre_state != CCG_POWERSTATE_S0) {
			cypd_set_power_state(CCG_POWERSTATE_S0, controller);
			/* only execute the error recovery when the system power on */
			if (pre_state != CCG_POWERSTATE_S0ix)
				task_set_event(TASK_ID_CYPD, CCG_EVT_PERFORM_ERROR_RECOVERY);
		}
		pre_state = CCG_POWERSTATE_S0;
		break;
	case POWER_S0ix:
	case POWER_S0S0ix: /* S0 -> S0ix */
		/* Do not update the same state again */
		if (pre_state != CCG_POWERSTATE_S0ix)
			cypd_set_power_state(CCG_POWERSTATE_S3, controller);
		pre_state = CCG_POWERSTATE_S0ix;
		break;
	default:
		break;
	}
}

/*****************************************************************
 * BB Retimer Functions
 ****************************************************************/

void enable_compliance_mode(int controller)
{
	int rv;
	uint32_t debug_register = 0xD0000000;
	int debug_ctl = 0x0100;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	/* Write 0xD0000000 to address 0x0048 */
	rv = cypd_write_reg_block(controller, CCG_ICL_BB_RETIMER_DAT_REG,
			(void *) &debug_register, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_DAT_REG fail");

	/* Write 0x0100 to address 0x0046 */
	rv = cypd_write_reg16(controller, CCG_ICL_BB_RETIMER_CMD_REG, debug_ctl);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_CMD_REG fail");
}

void disable_compliance_mode(int controller)
{
	int rv;
	uint32_t debug_register = 0x00000000;
	int debug_ctl = 0x0000;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	/* Write 0x00000000 to address 0x0048 */
	rv = cypd_write_reg_block(controller, CCG_ICL_BB_RETIMER_DAT_REG,
			(void *) &debug_register, 4);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_DAT_REG fail");

	/* Write 0x0000 to address 0x0046 */
	rv = cypd_write_reg16(controller, CCG_ICL_BB_RETIMER_CMD_REG, debug_ctl);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_CMD_REG fail");
}

void entry_tbt_mode(int controller)
{
	int rv;
	uint8_t force_tbt_mode = 0x01;
	int debug_ctl = 0x0100;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	/* Write 0x0100 to address 0x0046 */
	rv = cypd_write_reg16(controller, CCG_ICL_BB_RETIMER_CMD_REG, debug_ctl);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_CMD_REG fail");

	/* Write 0x01 to address 0x0040 */
	rv = cypd_write_reg8(controller, CCG_ICL_CTRL_REG, force_tbt_mode);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_CTRL_REG fail");
}

void exit_tbt_mode(int controller)
{
	int rv;
	uint8_t force_tbt_mode = 0x00;
	int debug_ctl = 0x0000;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	/* Write 0x00 to address 0x0040 */
	rv = cypd_write_reg8(controller, CCG_ICL_CTRL_REG, force_tbt_mode);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_CTRL_REG fail");

	/* Write 0x0000 to address 0x0046 */
	rv = cypd_write_reg16(controller, CCG_ICL_BB_RETIMER_CMD_REG, debug_ctl);
	if (rv != EC_SUCCESS)
		CPRINTS("Write CYP5525_ICL_BB_RETIMER_CMD_REG fail");
}

int check_tbt_mode(int controller)
{
	int rv;
	int data;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	rv = cypd_read_reg8(controller, CCG_ICL_STS_REG, &data);
	if (rv != EC_SUCCESS)
		CPRINTS("Read CYP5525_ICL_STS_REG fail");

	return data;
}

#ifdef CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE
/*****************************************************************
 * Customize response battery status
 ****************************************************************/

static struct pd_battery_cap_t pd_battery_cap;
static struct pd_battery_status_t pd_battery_status;
static int pd_batt_soc;
bool cypd_batt_update;

void cypd_customize_battery_cap(void)
{
	int i;
	uint32_t c, v;
	bool battery_can_discharge = (battery_is_present() == BP_YES) &
		battery_get_disconnect_state();

	/* only send status when PD ready */
	if (!are_cypd_chips_ready())
		return;

	if (!battery_can_discharge) {
		cypd_batt_update = false;
		pd_battery_cap.design_cap = 0x0000;
		pd_battery_cap.last_full_cap = 0x0000;
		pd_battery_cap.battery_type = 0x1;

	} else {
		cypd_batt_update = true;
		pd_battery_cap.reg = 0;
		pd_battery_cap.vid = VENDOR_ID;
		pd_battery_cap.pid = PRODUCT_ID;
		pd_battery_cap.battery_type = 0x0;

		if (battery_design_voltage(&v) == 0) {
			if (battery_design_capacity(&c) == 0) {
				/*
				 * Wh = (c * v) / 1000000
				 * 10th of a Wh = Wh * 10
				 */
				pd_battery_cap.design_cap = DIV_ROUND_NEAREST((c * v),
							100000);
			}
			if (battery_full_charge_capacity(&c) == 0) {
				/*
				 * Wh = (c * v) / 1000000
				 * 10th of a Wh = Wh * 10
				 */
				pd_battery_cap.last_full_cap = DIV_ROUND_NEAREST((c * v),
							100000);
			}
		}
	}

	for (i = 0; i < PD_CHIP_COUNT; i++)
		cypd_write_reg_block(i, CCG_BATTERT_STATE,
				&pd_battery_cap, sizeof(pd_battery_cap));

}

void cypd_customize_battery_status(void)
{
	int i, soc_wh;
	uint8_t	batt_info;
	uint32_t c, v;
	struct batt_params batt;
	bool battery_can_discharge = (battery_is_present() == BP_YES) &
		battery_get_disconnect_state();

	battery_get_params(&batt);

	/* only send status when PD ready */
	if (!are_cypd_chips_ready())
		return;

	/* only update data when soc change */
	if (batt.state_of_charge == pd_batt_soc)
		return;

	pd_batt_soc = batt.state_of_charge;

	if (!battery_can_discharge) {

		pd_battery_status.reg = 0x1;
		pd_battery_status.battery_info = 0;
		pd_battery_status.batt_present_cap = 0xFFFF;

	} else {

		/**
		 * if battery didn't set cap info at first time pd init
		 * need set again when battery ready.
		 * ex: resume from dead battery, or ac only boot and then plug-in batt
		 */
		if (!cypd_batt_update)
			cypd_customize_battery_cap();

		if (battery_design_voltage(&v) == 0) {
			if (battery_remaining_capacity(&c) == 0) {
				/*
				 * Wh = (c * v) / 1000000
				 * 10th of a Wh = Wh * 10
				 */
				soc_wh = DIV_ROUND_NEAREST((c * v), 100000);
			}
		}

		if (battery_status(&c) != 0) {
			batt_info = 0; /* batt not present */
		} else {
			if (c & STATUS_FULLY_CHARGED)
				/* Fully charged */
				batt_info = CCG6_BATT_IS_IDLE | CCG6_BATT_IS_PRESENT;
			else if (c & STATUS_DISCHARGING)
				/* Discharging */
				batt_info = CCG6_BATT_IS_DISCHARGING | CCG6_BATT_IS_PRESENT;
			else
				/* else battery is charging.*/
				batt_info = CCG6_BATT_IS_PRESENT;
		}

		pd_battery_status.reg = 0x1;
		pd_battery_status.battery_info = batt_info;
		pd_battery_status.batt_present_cap = soc_wh;
	}

	for (i = 0; i < PD_CHIP_COUNT; i++)
		cypd_write_reg_block(i, CCG_BATTERT_STATE,
				&pd_battery_status, sizeof(pd_battery_status));

}
DECLARE_HOOK(HOOK_AC_CHANGE, cypd_customize_battery_status, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, cypd_customize_battery_status, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_PD_CCG6_CUSTOMIZE_BATT_MESSAGE */

static enum ec_status bb_retimer_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_bb_retimer_control_mode *p = args->params;
	struct ec_response_bb_retimer_control_mode *r = args->response;

	r->status = 0;
	args->response_size = sizeof(*r);

	if (p->controller >= PD_CHIP_COUNT)
		return EC_RES_INVALID_PARAM;

	switch (p->modes) {
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
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BB_RETIMER_CONTROL, bb_retimer_control, EC_VER_MASK(0));
