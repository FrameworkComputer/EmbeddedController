/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "adc.h"
#include "builtin/assert.h"
#include "button.h"
#include "cec.h"
#include "cec_bitbang_chip.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_board_info.h"
#include "driver/cec/bitbang.h"
#include "driver/tcpm/tcpci.h"
#include "fw_config.h"
#include "gpio.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "peripheral_charger.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "throttle_ap.h"
#include "usbc_config.h"
#include "usbc_ppc.h"

#include <stdbool.h>

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

static void power_monitor(void);
DECLARE_DEFERRED(power_monitor);

/******************************************************************************/
/* USB-A charging control */

const int usb_port_enable[USB_PORT_COUNT] = {
	GPIO_EN_PP5000_USBA,
};
BUILD_ASSERT(ARRAY_SIZE(usb_port_enable) == USB_PORT_COUNT);

/******************************************************************************/

/* CEC ports */
static const struct bitbang_cec_config bitbang_cec_config_a = {
	.gpio_out = GPIO_HDMIA_CEC_OUT,
	.gpio_in = GPIO_HDMIA_CEC_IN,
	.gpio_pull_up = GPIO_HDMIA_CEC_PULL_UP,
	.timer = NPCX_CEC_BITBANG_TIMER_B,
};

const struct cec_config_t cec_config[] = {
	[CEC_PORT_0] = {
		.drv = &bitbang_cec_drv,
		.drv_config = &bitbang_cec_config_a,
		.offline_policy = NULL,
	},
};
BUILD_ASSERT(ARRAY_SIZE(cec_config) == CEC_PORT_COUNT);

int board_set_active_charge_port(int port)
{
	CPRINTS("Requested charge port change to %d", port);

	/*
	 * The charge manager may ask us to switch to no charger if we're
	 * running off USB-C only but upstream doesn't support PD. It requires
	 * that we accept this switch otherwise it triggers an assert and EC
	 * reset; it's not possible to boot the AP anyway, but we want to avoid
	 * resetting the EC so we can continue to do the "low power" LED blink.
	 */
	if (port == CHARGE_PORT_NONE)
		return EC_SUCCESS;

	if (port < 0 || CHARGE_PORT_COUNT <= port)
		return EC_ERROR_INVAL;

	if (port == charge_manager_get_active_charge_port())
		return EC_SUCCESS;

	/* Don't charge from a source port */
	if (board_vbus_source_enabled(port))
		return EC_ERROR_INVAL;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		int bj_active, bj_requested;

		if (charge_manager_get_active_charge_port() != CHARGE_PORT_NONE)
			/* Change is only permitted while the system is off */
			return EC_ERROR_INVAL;

		/*
		 * Current setting is no charge port but the AP is on, so the
		 * charge manager is out of sync (probably because we're
		 * reinitializing after sysjump). Reject requests that aren't
		 * in sync with our outputs.
		 */
		bj_active = !gpio_get_level(GPIO_EN_PPVAR_BJ_ADP_L);
		bj_requested = port == CHARGE_PORT_BARRELJACK;
		if (bj_active != bj_requested)
			return EC_ERROR_INVAL;
	}

	CPRINTS("New charger p%d", port);

	switch (port) {
	case CHARGE_PORT_TYPEC0:
	case CHARGE_PORT_TYPEC1:
	case CHARGE_PORT_TYPEC2:
		pd_set_power_supply_ready(port);
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);
		break;
	case CHARGE_PORT_BARRELJACK:
		/* Make sure BJ adapter is sourcing power */
		if (gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL))
			return EC_ERROR_INVAL;
		gpio_set_level(GPIO_EN_PPVAR_BJ_ADP_L, 0);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static uint8_t usbc_overcurrent;
static int32_t base_5v_power_s5;
static int32_t base_5v_power_z1;

/*
 * Power usage for each port as measured or estimated.
 * Units are milliwatts (5v x ma current)
 */

/* PP5000_S5 loads */
#define PWR_S5_BASE_LOAD (5 * 1431)
#define PWR_S5_REAR_HIGH (5 * 1737)
#define PWR_S5_REAR_LOW (5 * 1055)
#define PWR_S5_HDMI (5 * 580)
#define PWR_S5_MAX (5 * 10000)
#define REAR_DELTA (PWR_S5_REAR_HIGH - PWR_S5_REAR_LOW)

/* PP5000_Z1 loads */
#define PWR_Z1_BASE_LOAD (5 * 5)
#define PWR_Z1_C_HIGH (5 * 3600)
#define PWR_Z1_C_LOW (5 * 2000)
#define PWR_Z1_MAX (5 * 9000)
/*
 * Update the 5V power usage, assuming no throttling,
 * and invoke the power monitoring.
 */
static void update_5v_usage(void)
{
	int rear_ports = 0;

	/*
	 * Recalculate the 5V load, assuming no throttling.
	 */
	base_5v_power_s5 = PWR_S5_BASE_LOAD;
	if (!gpio_get_level(GPIO_USB_A0_OC_ODL)) {
		rear_ports++;
		base_5v_power_s5 += PWR_S5_REAR_LOW;
	}
	if (!gpio_get_level(GPIO_USB_A1_OC_ODL)) {
		rear_ports++;
		base_5v_power_s5 += PWR_S5_REAR_LOW;
	}
	/*
	 * Only 1 rear port can run higher power at a time.
	 */
	if (rear_ports > 0)
		base_5v_power_s5 += PWR_S5_REAR_HIGH - PWR_S5_REAR_LOW;
	if (!gpio_get_level(GPIO_HDMI_CONN_OC_ODL))
		base_5v_power_s5 += PWR_S5_HDMI;
	base_5v_power_z1 = PWR_Z1_BASE_LOAD;
	if (usbc_overcurrent)
		base_5v_power_z1 += PWR_Z1_C_HIGH;
	/*
	 * Invoke the power handler immediately.
	 */
	hook_call_deferred(&power_monitor_data, 0);
}
DECLARE_DEFERRED(update_5v_usage);
/*
 * Start power monitoring after ADCs have been initialised.
 */
DECLARE_HOOK(HOOK_INIT, update_5v_usage, HOOK_PRIO_INIT_ADC + 1);

static void port_ocp_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&update_5v_usage_data, 0);
}
/* Must come after other header files and interrupt handler declarations */
#include "gpio_list.h"

/******************************************************************************/
/*
 * Barrel jack power supply handling
 *
 * EN_PPVAR_BJ_ADP_L must default active to ensure we can power on when the
 * barrel jack is connected, and the USB-C port can bring the EC up fine in
 * dead-battery mode. Both the USB-C and barrel jack switches do reverse
 * protection, so we're safe to turn one on then the other off- but we should
 * only do that if the system is off since it might still brown out.
 */

#define ADP_DEBOUNCE_MS 1000 /* Debounce time for BJ plug/unplug */
/* Debounced connection state of the barrel jack */
static int8_t adp_connected = -1;
static void adp_connect_deferred(void)
{
	struct charge_port_info pi = { 0 };
	int connected = !gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL);

	/* Debounce */
	if (connected == adp_connected)
		return;
	if (connected)
		ec_bj_power(&pi.voltage, &pi.current);
	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &pi);
	adp_connected = connected;
}
DECLARE_DEFERRED(adp_connect_deferred);

/* IRQ for BJ plug/unplug. It shouldn't be called if BJ is the power source. */
void adp_connect_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&adp_connect_deferred_data, ADP_DEBOUNCE_MS * MSEC);
}

static void adp_state_init(void)
{
	ASSERT(CHARGE_PORT_ENUM_COUNT == CHARGE_PORT_COUNT);
	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	/* Report charge state from the barrel jack. */
	adp_connect_deferred();
}
DECLARE_HOOK(HOOK_INIT, adp_state_init, HOOK_PRIO_INIT_CHARGE_MANAGER + 1);

static void board_init(void)
{
	gpio_enable_interrupt(GPIO_BJ_ADP_PRESENT_ODL);
	gpio_enable_interrupt(GPIO_HDMI_CONN_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A0_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A1_OC_ODL);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_overcurrent_event(int port, int is_overcurrented)
{
	/* Check that port number is valid. */
	if ((port < 0) || (port >= CONFIG_USB_PD_PORT_MAX_COUNT))
		return;
	usbc_overcurrent = is_overcurrented;
	update_5v_usage();
}
/*
 * Power monitoring and management.
 *
 * the power budgets are met without letting the system fall into
 * power deficit (perhaps causing a brownout).
 *
 * There are 2 power budgets that need to be managed:
 * The overall goal is to gracefully manage the power demand so that
 *  - overall system power as measured on the main power supply rail.
 *  - 5V power delivered to the USB and HDMI ports.
 *
 * The actual system power demand is calculated from the VBUS voltage and
 * the input current (read from a shunt), averaged over 5 readings.
 * The power budget limit is from the charge manager.
 *
 * The 5V power cannot be read directly. Instead, we rely on overcurrent
 * inputs from the USB and HDMI ports to indicate that the port is in use
 * (and drawing maximum power).
 *
 * There are 3 throttles that can be applied (in priority order):
 *
 *  - Type A BC1.2 rear port restriction (3W)
 *  - Type C PD (throttle to 1.5A if sourcing)
 *  - Turn on PROCHOT, which immediately throttles the CPU.
 *
 *  The first 3 throttles affect both the system power and the 5V rails.
 *  The third is a last resort to force an immediate CPU throttle to
 *  reduce the overall power use.
 *
 *  The strategy is to determine what the state of the throttles should be,
 *  and to then turn throttles off or on as needed to match this.
 *
 *  This function runs on demand, or every 2 ms when the CPU is up,
 *  and continually monitors the power usage, applying the
 *  throttles when necessary.
 *
 *  All measurements are in milliwatts.
 */
#define THROT_TYPE_A_REAR BIT(0)
#define THROT_TYPE_C0 BIT(1)
#define THROT_TYPE_C1 BIT(2)
#define THROT_TYPE_C2 BIT(3)
#define THROT_PROCHOT BIT(4)

/*
 * Power gain if Type C port is limited.
 */
#define POWER_GAIN_TYPE_C 8800
/*
 * Power is averaged over 10 ms, with a reading every 2 ms.
 */
#define POWER_DELAY_MS 2
#define POWER_READINGS (10 / POWER_DELAY_MS)

static void power_monitor(void)
{
	static uint32_t current_state;
	static uint32_t history[POWER_READINGS];
	static uint8_t index;
	int32_t delay;
	uint32_t new_state = 0, diff;
	int32_t headroom_5v_s5 = PWR_S5_MAX - base_5v_power_s5;
	int32_t headroom_5v_z1 = PWR_Z1_MAX - base_5v_power_z1;

	/*
	 * If CPU is off or suspended, no need to throttle
	 * or restrict power.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF | CHIPSET_STATE_SUSPEND)) {
		/*
		 * Slow down monitoring, assume no throttling required.
		 */
		delay = 20 * MSEC;
		/*
		 * Clear the first entry of the power table so that
		 * it is re-initilalised when the CPU starts.
		 */
		history[0] = 0;
	} else {
		int32_t charger_mw;

		delay = POWER_DELAY_MS * MSEC;
		/*
		 * Get current charger limit (in mw).
		 * If not configured yet, skip.
		 */
		charger_mw = charge_manager_get_power_limit_uw() / 1000;
		if (charger_mw != 0) {
			int32_t gap, total, max, power;
			int i;

			/*
			 * Read power usage.
			 */
			power = (adc_read_channel(ADC_VBUS) *
				 adc_read_channel(ADC_PPVAR_IMON)) /
				1000;
			/* Init power table */
			if (history[0] == 0) {
				for (i = 0; i < POWER_READINGS; i++)
					history[i] = power;
			}
			/*
			 * Update the power readings and
			 * calculate the average and max.
			 */
			history[index] = power;
			index = (index + 1) % POWER_READINGS;
			total = 0;
			max = history[0];
			for (i = 0; i < POWER_READINGS; i++) {
				total += history[i];
				if (history[i] > max)
					max = history[i];
			}
			/*
			 * For Type-C power supplies, there is
			 * less tolerance for exceeding the rating,
			 * so use the max power that has been measured
			 * over the measuring period.
			 * For barrel-jack supplies, the rating can be
			 * exceeded briefly, so use the average.
			 */
			if (charge_manager_get_supplier() == CHARGE_SUPPLIER_PD)
				power = max;
			else
				power = total / POWER_READINGS;
			/*
			 * Calculate gap, and if negative, power
			 * demand is exceeding configured power budget, so
			 * throttling is required to reduce the demand.
			 */
			gap = charger_mw - power;
			/*
			 * Limiting type-A power rear ports.
			 */
			if (gap <= 0) {
				new_state |= THROT_TYPE_A_REAR;
				headroom_5v_s5 += REAR_DELTA;
			}
			/*
			 * If the type-C port is sourcing power,
			 * check whether it should be throttled.
			 */
			if (ppc_is_sourcing_vbus(0) && gap <= 0) {
				new_state |= THROT_TYPE_C0;
				headroom_5v_z1 += PWR_Z1_C_HIGH - PWR_Z1_C_LOW;
				if (!(current_state & THROT_TYPE_C0))
					gap += POWER_GAIN_TYPE_C;
			}
			/*
			 * If the type-C port is sourcing power,
			 * check whether it should be throttled.
			 */
			if (ppc_is_sourcing_vbus(1) && gap <= 0) {
				new_state |= THROT_TYPE_C1;
				headroom_5v_z1 += PWR_Z1_C_HIGH - PWR_Z1_C_LOW;
				if (!(current_state & THROT_TYPE_C1))
					gap += POWER_GAIN_TYPE_C;
			}
			/*
			 * If the type-C port is sourcing power,
			 * check whether it should be throttled.
			 */
			if (ppc_is_sourcing_vbus(2) && gap <= 0) {
				new_state |= THROT_TYPE_C2;
				headroom_5v_z1 += PWR_Z1_C_HIGH - PWR_Z1_C_LOW;
				if (!(current_state & THROT_TYPE_C2))
					gap += POWER_GAIN_TYPE_C;
			}
			/*
			 * As a last resort, turn on PROCHOT to
			 * throttle the CPU.
			 */
			if (gap <= 0)
				new_state |= THROT_PROCHOT;
		}
	}
	/*
	 * Check the 5v power usage and if necessary,
	 * adjust the throttles in priority order.
	 *
	 * Either throttle may have already been activated by
	 * the overall power control.
	 *
	 * We rely on the overcurrent detection to inform us
	 * if the port is in use.
	 *
	 *  - If type C not already throttled:
	 *	* If not overcurrent, prefer to limit type C [1].
	 *	* If in overcurrentuse:
	 *		- limit type A first [2]
	 *		- If necessary, limit type C [3].
	 *  - If type A not throttled, if necessary limit it [2].
	 */
	if (headroom_5v_z1 < 0) {
		/*
		 * Check whether type C is not throttled,
		 * and is not overcurrent.
		 */
		if (!((new_state & THROT_TYPE_C0) || usbc_overcurrent)) {
			/*
			 * [1] Type C not in overcurrent, throttle it.
			 */
			headroom_5v_z1 += PWR_Z1_C_HIGH - PWR_Z1_C_LOW;
			new_state |= THROT_TYPE_C0;
		}
		/*
		 * [2] If still under-budget, limit type C.
		 * No need to check if it is already throttled or not.
		 */
		if (headroom_5v_z1 < 0)
			new_state |= THROT_TYPE_C0;
	}
	if (headroom_5v_s5 < 0) {
		/*
		 * If type A rear not already throttled, and power still
		 * needed, limit type A rear.
		 */
		if (!(new_state & THROT_TYPE_A_REAR) && headroom_5v_s5 < 0) {
			headroom_5v_s5 += PWR_S5_REAR_HIGH - PWR_S5_REAR_LOW;
			new_state |= THROT_TYPE_A_REAR;
		}
	}
	/*
	 * Turn the throttles on or off if they have changed.
	 */
	diff = new_state ^ current_state;
	current_state = new_state;
	if (diff & THROT_PROCHOT) {
		int prochot = (new_state & THROT_PROCHOT) ? 0 : 1;

		gpio_set_level(GPIO_EC_PROCHOT_ODL, prochot);
	}
	if (diff & THROT_TYPE_C0) {
		enum tcpc_rp_value rp = (new_state & THROT_TYPE_C0) ?
						TYPEC_RP_1A5 :
						TYPEC_RP_3A0;

		ppc_set_vbus_source_current_limit(0, rp);
		tcpm_select_rp_value(0, rp);
		pd_update_contract(0);
	}
	if (diff & THROT_TYPE_C1) {
		enum tcpc_rp_value rp = (new_state & THROT_TYPE_C1) ?
						TYPEC_RP_1A5 :
						TYPEC_RP_3A0;

		ppc_set_vbus_source_current_limit(1, rp);
		tcpm_select_rp_value(1, rp);
		pd_update_contract(1);
	}
	if (diff & THROT_TYPE_C2) {
		enum tcpc_rp_value rp = (new_state & THROT_TYPE_C2) ?
						TYPEC_RP_1A5 :
						TYPEC_RP_3A0;

		ppc_set_vbus_source_current_limit(2, rp);
		tcpm_select_rp_value(2, rp);
		pd_update_contract(2);
	}
	if (diff & THROT_TYPE_A_REAR) {
		int typea_bc = (new_state & THROT_TYPE_A_REAR) ? 1 : 0;

		gpio_set_level(GPIO_USB_A_LOW_PWR0_OD, typea_bc);
		gpio_set_level(GPIO_USB_A_LOW_PWR1_OD, typea_bc);
	}
	hook_call_deferred(&power_monitor_data, delay);
}
