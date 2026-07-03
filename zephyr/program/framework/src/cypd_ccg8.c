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
#include "raa489300.h"
#include "task.h"
#include "ucsi.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_emsg.h"
#include "usb_tc_sm.h"
#include "util.h"
#include "throttle_ap.h"
#include "zephyr_console_shim.h"

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
	if (rv != EC_SUCCESS)
		CPRINTS("Write Reg8 0x%x fail!", reg);

	if (cypd_wait_for_ack(controller, 100) != EC_SUCCESS) {
		CPRINTS("%s timeout on interrupt", __func__);
		return EC_ERROR_INVAL;
	}
	rv = cypd_get_int(controller, &intr_status);
	if (rv != EC_SUCCESS)
		CPRINTS("Get INT Fail");

	if (intr_status & CCG_DEV_INTR && cmd_port == -1) {
		rv = cypd_read_reg16(controller, CCG_RESPONSE_REG, &event);
		if (rv != EC_SUCCESS)
			CPRINTS("fail to read DEV response");
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
 * CCG8 Setup Functions
 ****************************************************************/

#define CYPD_SETUP_CMDS_LEN 2
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
		{ CCG_EVENT_MASK_REG(0), 0x27ffff, 4, CCG_PORT0_INTR},
		{ CCG_EVENT_MASK_REG(1), 0x27ffff, 4, CCG_PORT1_INTR },
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

		if ((i % 2) >= pd_chip_config[controller].support_max_port)
			continue;

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

	/* Make sure the vbus fet control is configured before the PD controller
	 * auto enables one or more ports
	 */
	if (get_active_charge_pd_port() != -1) {
		for (i = 0; i < PD_PORT_COUNT; i++) {
			if (PORT_TO_CONTROLLER(i) == controller) {
				cypd_cfet_vbus_control(i, i == get_active_charge_pd_port(), true);
			}
		}
	}

	/*Notify the PD controller we are done and it can continue init*/
	rv = cypd_write_reg8_wait_ack(controller,
								CCG_PD_CONTROL_REG(0),
								CCG_PD_CMD_EC_INIT_COMPLETE);
	return EC_SUCCESS;
}

void update_system_power_state(int controller)
{
	static uint8_t pd_prev_power_state[PD_CHIP_COUNT];
	enum power_state ps = power_get_state();

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

	switch (ps) {
	case POWER_G3:
	case POWER_S5G3:
#ifdef CONFIG_PD_CCG8_CYPD_POWER_STATE_G3_SUPPORT
		if (pd_prev_power_state[controller] != CCG_POWERSTATE_G3) {
			cypd_set_power_state(CCG_POWERSTATE_G3, controller);
			pd_prev_power_state[controller] = CCG_POWERSTATE_G3;
		}
		break;
#endif
	case POWER_S3S5:
	case POWER_S4S5:
		if (pd_prev_power_state[controller] != CCG_POWERSTATE_S5) {
			cypd_set_power_state(CCG_POWERSTATE_S5, controller);
			pd_prev_power_state[controller] = CCG_POWERSTATE_S5;
		}
		break;
	case POWER_S0S3:
	case POWER_S0ixS3: /* S0ix -> S3 */
		if (pd_prev_power_state[controller] != CCG_POWERSTATE_S3) {
			cypd_set_power_state(CCG_POWERSTATE_S3, controller);
			if (pd_prev_power_state[controller] == CCG_POWERSTATE_G3
			 || pd_prev_power_state[controller] == CCG_POWERSTATE_S5) {
				task_set_event(TASK_ID_CYPD, CCG_EVT_PERFORM_ERROR_RECOVERY);
			}
			pd_prev_power_state[controller] = CCG_POWERSTATE_S3;
		}
		break;
	case POWER_S0:
	case POWER_G3S5:
	case POWER_S4S3:
	case POWER_S5S3:
	case POWER_S3S0:
	case POWER_S0ixS0: /* S0ix -> S0 */
		if (pd_prev_power_state[controller] != CCG_POWERSTATE_S0) {
			cypd_set_power_state(CCG_POWERSTATE_S0, controller);
			if (pd_prev_power_state[controller] == CCG_POWERSTATE_G3
			 || pd_prev_power_state[controller] == CCG_POWERSTATE_S5) {
				task_set_event(TASK_ID_CYPD, CCG_EVT_PERFORM_ERROR_RECOVERY);
			}
			pd_prev_power_state[controller] = CCG_POWERSTATE_S0;
		}
		break;
	case POWER_S0ix:
	case POWER_S3S0ix: /* S3 -> S0ix */
	case POWER_S0S0ix: /* S0 -> S0ix */
		if (pd_prev_power_state[controller] != CCG_POWERSTATE_S0ix) {
			cypd_set_power_state(CCG_POWERSTATE_S0ix, controller);
			pd_prev_power_state[controller] = CCG_POWERSTATE_S0ix;
		}
		break;

	default:
		break;
	}

}


#ifdef CONFIG_PD_CCG8_CUSTOMIZE_BATT_MESSAGE
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

	/* Type=Battery_Capabilities */
	pd_battery_cap.type = 0x02;
	/* B2=1 for all ports B0:1 command = 0 for write */
	pd_battery_cap.command = 0x04;
	/* Type=Battery_Capabilities */
	pd_battery_cap.size = 13;
	pd_battery_cap.reserved = 0x00;

	/* 0=first fixed battery */
	pd_battery_cap.battery_slot_id = 0x00;
	/* 0=Battery_Capabilities valid */
	pd_battery_cap.invalid_ref_flag = 0x00;
	pd_battery_cap.reserved_hdr = 0x00;

	if (!battery_can_discharge) {
		cypd_batt_update = false;
		pd_battery_cap.design_cap = 0x0000;
		pd_battery_cap.last_full_cap = 0x0000;
		pd_battery_cap.battery_type = 0x1;

	} else {
		cypd_batt_update = true;
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

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		if (!cypd_contoller_is_powered(i))
			continue;

		cypd_write_reg_block(i, CCG_WRITE_DATA_MEMORY_REG(0, 0),
			&pd_battery_cap, sizeof(pd_battery_cap));

		cypd_write_reg8(i, CCG_PD_CONTROL_REG(0),
			CCG_PD_CMD_RW_PD_RESPONSE_DATA);
	}

}

void cypd_customize_battery_status(void)
{
	int i, soc_wh;
	int force_update = 0;
	uint8_t	batt_info;
	uint32_t c, v;
	struct batt_params batt;
	static int pre_ac_state;
	static enum ccg_pd_state pd_pre_state[PD_CHIP_COUNT];
	int curr_ac_state = extpower_is_present();
	bool ac_changed = (pre_ac_state != curr_ac_state);
	bool battery_can_discharge = (battery_is_present() == BP_YES) &
		battery_get_disconnect_state();

	battery_get_params(&batt);

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		/*
		 * If any PD chip's state has changed since the last check,
		 * need to update the battery status to ensure PD receives
		 * the latest information.
		 */
		if (pd_chip_config[i].state != pd_pre_state[i]) {
			force_update = 1;
			pd_pre_state[i] = pd_chip_config[i].state;
		}
	}

	/* Update data when soc change/ac change/force update */
	if ((batt.state_of_charge == pd_batt_soc) && !ac_changed && !force_update)
		return;

	pre_ac_state = curr_ac_state;

	pd_batt_soc = batt.state_of_charge;

	/* Type=Battery_Status */
	pd_battery_status.type = 0x01;
	/* B2=1 for all ports B0:1 command = 0 for write */
	pd_battery_status.command = 0x04;
	/* Type=BatteryStatus */
	pd_battery_status.size = 8;
	pd_battery_status.reserved = 0x00;

	/* 0=first fixed battery */
	pd_battery_status.battery_slot_id = 0x00;
	/* 0=Battery_Status valid */
	pd_battery_status.invalid_ref_flag = 0x00;
	pd_battery_status.reserved_hdr = 0x00;

	if (!battery_can_discharge) {
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
				batt_info = BSDO_BATT_IS_IDLE | BSDO_BATT_IS_PRESENT;
			else if (c & STATUS_DISCHARGING)
				/* Discharging */
				batt_info = BSDO_BATT_IS_DISCHARGING | BSDO_BATT_IS_PRESENT;
			else
				/* else battery is charging.*/
				batt_info = BSDO_BATT_IS_PRESENT;
		}

		pd_battery_status.battery_info = batt_info;
		pd_battery_status.batt_present_cap = soc_wh;
	}

	for (i = 0; i < PD_CHIP_COUNT; i++) {
		if (!cypd_contoller_is_powered(i))
			continue;

		cypd_write_reg_block(i, CCG_WRITE_DATA_MEMORY_REG(0, 0),
			&pd_battery_status, sizeof(pd_battery_status));

		cypd_write_reg8(i, CCG_PD_CONTROL_REG(0),
			CCG_PD_CMD_RW_PD_RESPONSE_DATA);
	}

}
#endif /* CONFIG_PD_CCG8_CUSTOMIZE_BATT_MESSAGE */

#ifdef CONFIG_PD_CCG8_EPR

/*****************************************************************
 * CCG8 EPR Functions
 ****************************************************************/

static uint8_t pd_epr_in_progress;

int epr_progress_status(void)
{
	return pd_epr_in_progress;
}

void clear_epr_progress_mask(void)
{
	pd_epr_in_progress &= ~EPR_PROCESS_MASK;
}

void clear_epr_progress(void)
{
    /* clear the EPR progress when the adapter is removed */
	pd_epr_in_progress &= EPR_PROCESS_MASK;
}

static void epr_flow_pending_deferred(void)
{
	int port_idx;
	int charge_port = get_active_charge_pd_port();

	/**
	 * Sometimes, EC does not receive the EPR event/NOT support event from PD chip.
	 * Retry the last action.
	 */
	for (port_idx = 0; port_idx < PD_PORT_COUNT; port_idx++) {
		if (pd_epr_in_progress & BIT(port_idx)) {
			if (pd_port_states[port_idx].epr_retry_count > 4) {
				/* restore the input current limit if we retry 4 times */
				pd_port_states[port_idx].epr_retry_count = 0;
				pd_port_states[port_idx].epr_support = 0;
				pd_epr_in_progress &= EPR_PROCESS_MASK;
				if (charge_port != -1) {
					int controller = PORT_TO_CONTROLLER(charge_port);
					int port = PORT_TO_CONTROLLER_PORT(charge_port);

					cypd_update_port_state(controller, port);
				}
			}
			/**
			 * There is a low risk situation.
			 * If both the EXIT EPR and ENTER EPR flags are set simultaneously,
			 * it will cause epr_retry_count to be incremented twice.
			 */
			if (pd_epr_in_progress & EXIT_EPR) {
				CPRINTS("C%d exit EPR stuck, retry!", port_idx);
				exit_epr_mode();
				pd_port_states[port_idx].epr_retry_count++;
			}

			if (pd_epr_in_progress & ENTER_EPR) {
				CPRINTS("C%d enter EPR stuck, retry!", port_idx);
				enter_epr_mode();
				pd_port_states[port_idx].epr_retry_count++;
			}

		} else
			pd_port_states[port_idx].epr_retry_count = 0;
	}
}
DECLARE_DEFERRED(epr_flow_pending_deferred);

#ifdef CONFIG_PLATFORM_EC_CHARGER_RAA489300
/*****************************************************************
 * Raa489300 3-level-buck transition EPR mode function
 ****************************************************************/
static struct epr_buck_transition_ctx epr_buck_ctx;

__overridable int board_set_buck_mode(enum level_buck_mode mode)
{
	return EC_SUCCESS;
}

__overridable int board_confirm_buck_transition_ready(enum level_buck_mode mode)
{
	return EC_SUCCESS;
}

static void buck_ready_for_epr(void);
DECLARE_DEFERRED(buck_ready_for_epr);

static void handle_pd_epr_mode(bool is_enter_epr)
{
	int port_idx;

	uint8_t pd_cmd = is_enter_epr
		? CCG_PD_CMD_INITIATE_EPR_ENTRY
		: CCG_PD_CMD_INITIATE_EPR_EXIT;

	for (port_idx = 0; port_idx < PD_PORT_COUNT; port_idx++) {
		if (pd_epr_in_progress & BIT(port_idx)) {
			cypd_write_reg8(PORT_TO_CONTROLLER(port_idx),
				CCG_PD_CONTROL_REG(PORT_TO_CONTROLLER_PORT(port_idx)),
				pd_cmd);
		}
	}

	hook_call_deferred(&epr_flow_pending_deferred_data,
		is_enter_epr ? 200 * MSEC : 500 * MSEC);
}

#define WAIT_TRANSITION_TIME (200 * MSEC)
static void buck_ready_for_epr(void)
{
	bool is_enter_epr = (epr_buck_ctx.progress == PD_PROGRESS_ENTER_EPR_MODE);
	int mode = is_enter_epr ? LEVEL_BUCK_ENTER_EPR : LEVEL_BUCK_EXIT_EPR;

	/* make sure enter EPR mode only process in S0 state */
	if (is_enter_epr && (!chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON) ||
		!extpower_is_present())) {
		CPRINTS("Enter EPR aborted: not in S0 or no power");
		epr_buck_ctx.phase = BUCK_PHASE_IDLE;
		return;
	}

	/* STEP 1: Buck mode transition handling */
	if (epr_buck_ctx.phase == BUCK_PHASE_SET_MODE) {
		/*
		 * For multi port EPR + EPR.
		 * Enter EPR: When PSM is already in buck mode, do nothing.
		 */
		if (is_enter_epr && level_buck_check_expected_state(mode, NULL) == EC_SUCCESS) {
			goto ready_success;
		}

		/* Attempt to set buck mode */
		if (board_set_buck_mode(mode) == EC_SUCCESS) {
			epr_buck_ctx.retry_count = 0;
			epr_buck_ctx.phase = BUCK_PHASE_CHECK_READY;
		}
	}

	/* STEP 2: Check if the buck regulator is ready */
	if (epr_buck_ctx.phase == BUCK_PHASE_CHECK_READY) {
		if (board_confirm_buck_transition_ready(mode) == EC_SUCCESS) {
			goto ready_success;
		}
	}

	/* Retry mechanism */
	if (++epr_buck_ctx.retry_count > 4) {
		if (is_enter_epr) {
			CPRINTS("3Level-Buck enter epr failed, reverting to SPR mode");
			board_set_buck_mode(LEVEL_BUCK_SPR);
		} else {
			CPRINTS("3Level-Buck exit epr failed, reverting to EPR mode");
			board_set_buck_mode(LEVEL_BUCK_EPR);
		}
		epr_buck_ctx.retry_count = 0;
		epr_buck_ctx.phase = BUCK_PHASE_IDLE;
		return;
	}

	CPRINTS("3Level-Buck %s retry %d",
		is_enter_epr ? "enter epr" : "exit epr", epr_buck_ctx.retry_count);
	hook_call_deferred(&buck_ready_for_epr_data, WAIT_TRANSITION_TIME);
	return;

	/* STEP 3: Execute the CCG command to enter the EPR mode */
ready_success:
	CPRINTS("3Level-Buck %s ready", is_enter_epr ? "enter epr" : "exit epr");
	epr_buck_ctx.retry_count = 0;
	epr_buck_ctx.phase = BUCK_PHASE_IDLE;
	handle_pd_epr_mode(is_enter_epr);
}
#endif

void enter_epr_mode(void)
{
	int port_idx;

	__ASSERT(BIT(PD_PORT_COUNT) < EXIT_EPR,
			"PD port bits must not exceed EXIT_EPR bit in %s.", __func__);

	/**
	 * Only enter EPR mode when the system in S0 state.
	 * 1. Resume from S0i3 mode
	 * 2. Power up from S5/G3 state (after error recovery, will enter EPR mode automatically)
	 * 3. battery in normal mode
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF) ||
		battery_is_cut_off() || battery_cutoff_in_progress())
		return;

	/**
	 * PD negotiation completed and in Sink Role,
	 * execute the CCG command to enter the EPR mode
	 */
	for (port_idx = 0; port_idx < PD_PORT_COUNT; port_idx++) {
		if ((pd_port_states[port_idx].pd_state) &&
			(pd_port_states[port_idx].power_role == PD_ROLE_SINK) &&
			(pd_port_states[port_idx].epr_status == EPR_INACTIVE) &&
			(pd_port_states[port_idx].epr_support == 1)) {

			/* BIT(6),BIT(7): epr in progress, BIT(0) - BIT(5) which port */
			pd_epr_in_progress |= (BIT(port_idx) + ENTER_EPR);

			/* avoid the pmf is higher when the system resume from S0ix */
			update_cpu_power_limit_events(BIT(PD_PROGRESS_ENTER_EPR_MODE),
				!!(pd_epr_in_progress & ~EPR_PROCESS_MASK));

			if (battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED
				&& get_active_charge_pd_port() == port_idx) {
				/* Enable learn mode to discharge on AC */
				charger_discharge_on_ac(1);

				/* Set input current to 0mA */
				charger_set_input_current_limit(0, 0);
			}
#ifdef CONFIG_PLATFORM_EC_CHARGER_RAA489300
			if (epr_buck_ctx.phase == BUCK_PHASE_IDLE) {
				epr_buck_ctx.phase = BUCK_PHASE_SET_MODE;
				epr_buck_ctx.progress = PD_PROGRESS_ENTER_EPR_MODE;
				hook_call_deferred(&buck_ready_for_epr_data, 0);
			}
#else
			cypd_write_reg8(PORT_TO_CONTROLLER(port_idx),
					CCG_PD_CONTROL_REG(PORT_TO_CONTROLLER_PORT(port_idx)),
					CCG_PD_CMD_INITIATE_EPR_ENTRY);

			hook_call_deferred(&epr_flow_pending_deferred_data, 200 * MSEC);
#endif
		}
	}
}
DECLARE_DEFERRED(enter_epr_mode);

void cypd_enter_epr_mode(int delay)
{
	hook_call_deferred(&enter_epr_mode_data, delay * MSEC);
}

void enter_epr_mode_without_battery(void)
{
	if ((battery_get_disconnect_state() == BATTERY_DISCONNECTED) ||
	    (battery_is_present() != BP_YES))
		enter_epr_mode();
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, enter_epr_mode_without_battery, HOOK_PRIO_DEFAULT);

void exit_epr_mode(void)
{
	int port_idx;

	__ASSERT(BIT(PD_PORT_COUNT) < EXIT_EPR,
			"PD port bits must not exceed EXIT_EPR bit in %s.", __func__);

	for (port_idx = 0; port_idx < PD_PORT_COUNT; port_idx++) {
		if (pd_port_states[port_idx].epr_status == EPR_ACTIVE) {

			/* BIT(6),BIT(7): epr in progress, BIT(0) - BIT(5) which port */
			pd_epr_in_progress |= (BIT(port_idx) + EXIT_EPR);

			/* do not set learn mode when battery is cut off */
			if (!battery_cutoff_in_progress() && !battery_is_cut_off() &&
				(battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED)
				&& get_active_charge_pd_port() == port_idx) {
				/* Enable learn mode to discharge on AC */
				charger_discharge_on_ac(1);

				/* Set input current to 0mA */
				charger_set_input_current_limit(0, 0);
			} else {
				update_cpu_power_limit_events(BIT(PD_PROGRESS_EXIT_EPR_MODE),
						!!(pd_epr_in_progress & ~EPR_PROCESS_MASK));
			}

#ifdef CONFIG_PLATFORM_EC_CHARGER_RAA489300
			if (epr_buck_ctx.phase == BUCK_PHASE_IDLE) {
				epr_buck_ctx.phase = BUCK_PHASE_SET_MODE;
				epr_buck_ctx.progress = PD_PROGRESS_EXIT_EPR_MODE;
				hook_call_deferred(&buck_ready_for_epr_data, 0);
			}
#else
			cypd_write_reg8(PORT_TO_CONTROLLER(port_idx),
					CCG_PD_CONTROL_REG(PORT_TO_CONTROLLER_PORT(port_idx)),
					CCG_PD_CMD_INITIATE_EPR_EXIT);

			hook_call_deferred(&epr_flow_pending_deferred_data, 500 * MSEC);
#endif
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, exit_epr_mode, HOOK_PRIO_FIRST);

void cypd_update_epr_state(int controller, int port, int response_len)
{
	int rv;
	uint8_t data[16] = {0};
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;
	int port_idx = (controller << 1) + port;

	__ASSERT(controller < PD_CHIP_COUNT, "Invalid PD chip controller id in %s.", __func__);

	if (!cypd_contoller_is_powered(controller))
		return;

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
#ifdef CONFIG_PLATFORM_EC_CHARGER_RAA489300
			/* EPR fail, switch to PTM mode */
			board_set_buck_mode(LEVEL_BUCK_SPR);
#endif
			/* EPR fail, do not retry */
			pd_port_states[port_idx].epr_status = EPR_DISABLED;
		}
	}

	pd_epr_in_progress &= ~BIT((controller << 1) + port);
}
#endif
