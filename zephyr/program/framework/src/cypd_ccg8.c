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

#ifdef CONFIG_PD_CCG8_EPR
static uint8_t pd_epr_in_progress;

#endif

#ifdef CONFIG_PD_CCG8_EPR
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
