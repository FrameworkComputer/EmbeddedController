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
#include "gpio.h"
#include "hooks.h"
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

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

DECLARE_DEFERRED(update_power_state_deferred);


/*****************************************************************
 * Charger Port C-FET control Functions
 ****************************************************************/

static uint8_t pd_c_fet_active_port;

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
	int next_charge_port = get_active_charge_pd_port();

	CPRINTS("%s port %d, prev:%d", __func__, charge_port, next_charge_port);

	if (next_charge_port == charge_port) {
		/* in the case of hard reset, we do not turn off the old
		 * port, but the PD will implicitly clear the port
		 * so we need to turn on the vbus control again.
		 */
		cypd_cfet_vbus_control(charge_port, true, true);
		return EC_SUCCESS;
	}


	if (next_charge_port != -1 &&
		next_charge_port != charge_port) {
		/* Turn off the previous charge port before turning on the next port */
		cypd_cfet_vbus_control(next_charge_port, false, true);
	}

	for (i = 0; i < PD_PORT_COUNT; i++) {
		/* Just brute force all ports, we want to make sure
		 * we always update all ports in case a PD controller rebooted or some
		 * other error happens that we are not tracking state with.
		 */
		cypd_cfet_vbus_control(i, i == charge_port, true);
	}
	next_charge_port = charge_port;
	hook_call_deferred(&update_power_state_deferred_data, 100 * MSEC);

	return EC_SUCCESS;
}

/*****************************************************************
 * CCG8 Setup Functions
 ****************************************************************/

#define CYPD_SETUP_CMDS_LEN 2
__overridable int cypd_setup(int controller)
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

/*****************************************************************
 * Error Recovery Functions
 ****************************************************************/

static void perform_error_recovery(int controller)
{
	int i;
	uint8_t data[2] = {0x00, CCG_PD_USER_CMD_TYPEC_ERR_RECOVERY};
	uint32_t batt_os_percentage = get_system_percentage();

	if (controller < 2)
		for (i = 0; i < 2; i++) {
			if (!((controller*2 + i) == get_active_charge_pd_port() &&
				battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED)) {

				data[0] = PORT_TO_CONTROLLER_PORT(i);
				cypd_write_reg_block(PORT_TO_CONTROLLER(i),
									CCG_DPM_CMD_REG,
									data, 2);
			}
		}
	else {
		/* Hard reset all ports that are not supplying power in dead battery mode */
		for (i = 0; i < PD_PORT_COUNT; i++) {
			if (!(i == get_active_charge_pd_port() &&
			    battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED)) {

				if ((pd_port_states[i].c_state == CCG_STATUS_SOURCE) &&
				   (batt_os_percentage < 3) && (i == get_active_charge_pd_port()))
					continue;

				CPRINTS("Hard reset %d", i);
				data[0] = PORT_TO_CONTROLLER_PORT(i);
				cypd_write_reg_block(PORT_TO_CONTROLLER(i),
									CCG_DPM_CMD_REG,
									data, 2);
			}
		}
	}
}

enum power_state pd_prev_power_state = POWER_G3;
__override void update_system_power_state(int controller)
{
	enum power_state ps = power_get_state();

	switch (ps) {
	case POWER_G3:
	case POWER_S5G3:
		pd_prev_power_state = POWER_G3;
		cypd_set_power_state(CCG_POWERSTATE_G3, controller);
		break;
	case POWER_S5:
	case POWER_S3S5:
	case POWER_S4S5:
		pd_prev_power_state = POWER_S5;
		cypd_set_power_state(CCG_POWERSTATE_S5, controller);
		break;
	case POWER_S3:
	case POWER_S4S3:
	case POWER_S5S3:
	case POWER_S0S3:
	case POWER_S0ixS3: /* S0ix -> S3 */
		cypd_set_power_state(CCG_POWERSTATE_S3, controller);
		if (pd_prev_power_state < POWER_S3) {
			perform_error_recovery(controller);
			pd_prev_power_state = ps;
		}
		break;
	case POWER_S0:
	case POWER_S3S0:
	case POWER_S0ixS0: /* S0ix -> S0 */
		cypd_set_power_state(CCG_POWERSTATE_S0, controller);
		if (pd_prev_power_state < POWER_S3) {
			perform_error_recovery(controller);
			pd_prev_power_state = ps;
		}
		break;
	case POWER_S0ix:
	case POWER_S3S0ix: /* S3 -> S0ix */
	case POWER_S0S0ix: /* S0 -> S0ix */
		cypd_set_power_state(CCG_POWERSTATE_S0ix, controller);
		break;

	default:
		break;
	}

}

#ifdef CONFIG_PD_CCG8_EPR

/*****************************************************************
 * CCG8 EPR Functions
 ****************************************************************/

static uint8_t pd_epr_in_progress;

int epr_progress_status(void)
{
	return pd_epr_in_progress;
}

void clear_erp_progress_mask(void)
{
	pd_epr_in_progress &= ~EPR_PROCESS_MASK;
}

void clear_erp_progress(void)
{
    /* clear the EPR progress when the adapter is removed */
	pd_epr_in_progress &= EPR_PROCESS_MASK;
}

static void epr_flow_pending_deferred(void)
{
	static int retry_count;

	/**
	 * Sometimes, EC does not receive the EPR event/NOT support event from PD chip.
	 * Retry the last action.
	 */

	if (!!(pd_epr_in_progress & ~EPR_PROCESS_MASK)) {
		if (retry_count > 4) {
			/* restore the input current limit if we retry 4 times */
			retry_count = 0;
			pd_epr_in_progress &= EPR_PROCESS_MASK;
			if (get_active_charge_pd_port() != -1)
				cypd_update_port_state((get_active_charge_pd_port() & 0x02) >> 1,
					get_active_charge_pd_port() & BIT(0));
		}

		if (pd_epr_in_progress & EXIT_EPR) {
			CPRINTS("Exit EPR stuck, retry!");
			exit_epr_mode();
			retry_count++;
		}

		if (pd_epr_in_progress & ENTER_EPR) {
			CPRINTS("enter EPR stuck, retry!");
			enter_epr_mode();
			retry_count++;
		}

	} else
		retry_count = 0;
}
DECLARE_DEFERRED(epr_flow_pending_deferred);

void enter_epr_mode(void)
{
	int port_idx;

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
			(pd_port_states[port_idx].epr_active == 0) &&
			(pd_port_states[port_idx].epr_support == 1)) {

			/* BIT(4): epr in progress, BIT(1) - BIT(3) which port */
			pd_epr_in_progress |= (BIT(port_idx) + ENTER_EPR);

			/* avoid the pmf is higher when the system resume from S0ix */
			update_pmf_events(BIT(PD_PROGRESS_ENTER_EPR_MODE),
				!!(pd_epr_in_progress & ~EPR_PROCESS_MASK));

			if (battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED) {
				/* Enable learn mode to discharge on AC */
				board_discharge_on_ac(1);

				/* Set input current to 0mA */
				charger_set_input_current_limit(0, 0);
			}

			cypd_write_reg8((port_idx & 0x2) >> 1,
					CCG_PD_CONTROL_REG(port_idx & 0x1),
					CCG_PD_CMD_INITIATE_EPR_ENTRY);

			hook_call_deferred(&epr_flow_pending_deferred_data, 200 * MSEC);
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

	for (port_idx = 0; port_idx < PD_PORT_COUNT; port_idx++) {
		if (pd_port_states[port_idx].epr_active == 1) {

			/* BIT(4): epr in progress, BIT(1) - BIT(3) which port */
			pd_epr_in_progress |= (BIT(port_idx) + EXIT_EPR);

			/* do not set learn mode when battery is cut off */
			if (!battery_cutoff_in_progress() && !battery_is_cut_off() &&
				(battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED)) {
				/* Enable learn mode to discharge on AC */
				board_discharge_on_ac(1);

				/* Set input current to 0mA */
				charger_set_input_current_limit(0, 0);
			} else {
				update_pmf_events(BIT(PD_PROGRESS_EXIT_EPR_MODE),
						!!(pd_epr_in_progress & ~EPR_PROCESS_MASK));
			}

			cypd_write_reg8((port_idx & 0x2) >> 1,
					CCG_PD_CONTROL_REG(port_idx & 0x1),
					CCG_PD_CMD_INITIATE_EPR_EXIT);

			hook_call_deferred(&epr_flow_pending_deferred_data, 500 * MSEC);
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, exit_epr_mode, HOOK_PRIO_FIRST);

void cypd_update_epr_state(int controller, int port, int response_len)
{
	int rv;
	uint8_t data[16];
	uint16_t i2c_port = pd_chip_config[controller].i2c_port;
	uint16_t addr_flags = pd_chip_config[controller].addr_flags;
	int port_idx = (controller << 1) + port;

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
			/* EPR fail, do not retry */
			pd_port_states[port_idx].epr_active = 0xff;
		}
	}

	pd_epr_in_progress &= ~BIT((controller << 1) + port);
}
#endif
