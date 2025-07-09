/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "charge_manager.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "power.h"
#include "system.h"
#include "tcpm/tcpci.h"
#include "uart.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#include <zephyr/logging/log.h>

/*
 * Enable interrupts
 */
test_export_static void board_bj_init(void)
{
	/*
	 * Enable USB-C interrupts.
	 */
	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_bj_adp_present));
}
DECLARE_HOOK(HOOK_INIT, board_bj_init, HOOK_PRIO_DEFAULT);

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

/******************************************************************************/
/*
 * Since dirks has no battery, it must source all of its power from either
 * USB-C or the barrel jack (preferred). Dirks operates in continuous safe
 * mode (charge_manager_leave_safe_mode() will never be called), which
 * modifies port selection as follows:
 *
 * - Dual-role / dedicated capability of the port partner is ignored.
 * - Charge ceiling on PD voltage transition is ignored.
 * - CHARGE_PORT_NONE will never be selected.
 */

/* List of BJ adapters */
enum bj_adapter {
	BJ_NONE,
	BJ_65W_19V,
};

/* Barrel-jack power adapter ratings. */
static const struct charge_port_info bj_adapters[] = {
	[BJ_NONE] = { .current = 0, .voltage = 0 },
	[BJ_65W_19V] = { .current = 3420, .voltage = 19000 },
};
#define BJ_ADP_RATING_DEFAULT BJ_65W_19V /* BJ power ratings default */
#define ADP_DEBOUNCE_MS 1000 /* Debounce time for BJ plug/unplug */

/* Debounced connection state of the barrel jack */
static int8_t bj_adp_connected = -1;
static void adp_connect_deferred(void)
{
	const struct charge_port_info *pi;
	int connected =
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_bj_adp_present));

	/* Debounce */
	if (connected == bj_adp_connected)
		return;

	if (connected) {
		pi = &bj_adapters[BJ_ADP_RATING_DEFAULT];
	} else {
		/* No barrel-jack, zero out this power supply */
		pi = &bj_adapters[BJ_NONE];
	}
	/* This will result in a call to board_set_active_charge_port */
	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, pi);
	bj_adp_connected = connected;
}
DECLARE_DEFERRED(adp_connect_deferred);

/* IRQ for BJ plug/unplug. It shouldn't be called if BJ is the power source. */
void adp_connect_interrupt(enum gpio_signal signal)
{
	hook_call_deferred(&adp_connect_deferred_data,
			   ADP_DEBOUNCE_MS * USEC_PER_MSEC);
}

int board_set_active_charge_port(int port)
{
	const int active_port = charge_manager_get_active_charge_port();

	LOG_INF("Requested charge port change to %d", port);

	if (port < 0 || CHARGE_PORT_COUNT <= port)
		return EC_ERROR_INVAL;

	if (port == active_port)
		return EC_SUCCESS;

	/* Don't sink from a source port */
	if (board_vbus_source_enabled(port))
		return EC_ERROR_INVAL;

	if (!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		int bj_requested;

		if (charge_manager_get_active_charge_port() != CHARGE_PORT_NONE)
			/* Change is only permitted while the system is off */
			return EC_ERROR_INVAL;

		/*
		 * Current setting is no charge port but the AP is on, so the
		 * charge manager is out of sync (probably because we're
		 * reinitializing after sysjump). Reject requests that aren't
		 * in sync with our outputs.
		 */
		bj_requested = port == CHARGE_PORT_BARRELJACK;
		if (bj_adp_connected != bj_requested)
			return EC_ERROR_INVAL;
	}

	LOG_INF("New charger p%d", port);

	switch (port) {
	case CHARGE_PORT_TYPEC0:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_bj_adp_od),
				0);
		ppc_vbus_sink_enable(USBC_PORT_C0, 1);
		break;
	case CHARGE_PORT_BARRELJACK:
		/* Make sure BJ adapter is sourcing power */
		if (!gpio_pin_get_dt(
			    GPIO_DT_FROM_NODELABEL(gpio_bj_adp_present)))
			return EC_ERROR_INVAL;
		ppc_vbus_sink_enable(USBC_PORT_C0, 0);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_en_ppvar_bj_adp_od),
				1);
		break;
	default:
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static void board_charge_manager_init(void)
{
	enum charge_port port;

	/*
	 * Initialize all charge suppliers to 0. The charge manager waits until
	 * all ports have reported in before doing anything.
	 */
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		for (int j = 0; j < CHARGE_SUPPLIER_COUNT; j++)
			charge_manager_update_charge(j, i, NULL);
	}

	port = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_bj_adp_present)) ?
		       CHARGE_PORT_BARRELJACK :
		       CHARGE_PORT_TYPEC0;
	LOG_INF("Power source is p%d (%s)", port,
		port == CHARGE_PORT_TYPEC0 ? "USB-C" : "BJ");

	/* Initialize the power source supplier */
	switch (port) {
	case CHARGE_PORT_TYPEC0:
		typec_set_input_current_limit(port, 3000, 5000);
		break;
	case CHARGE_PORT_BARRELJACK:
		charge_manager_update_charge(
			CHARGE_SUPPLIER_DEDICATED, DEDICATED_CHARGE_PORT,
			&bj_adapters[BJ_ADP_RATING_DEFAULT]);
		break;
	}

	/* Report charge state from the barrel jack. */
	adp_connect_deferred();
}
DECLARE_HOOK(HOOK_INIT, board_charge_manager_init,
	     HOOK_PRIO_INIT_CHARGE_MANAGER + 1);

/*
 * Power monitoring and management.
 *
 * The overall goal is to gracefully manage the power demand so that the power
 * budgets are met without letting the system fall into power deficit (perhaps
 * causing a brownout).
 *
 * The actual system power demand is calculated from the VBUS voltage and the
 * input current (read from a shunt), averaged over 10 readings. The power
 * budget limit is from the charge manager.
 *
 * Throttles which can be applied:
 *  - Throttle Type-C power from 3A to 1.5A if sourcing.
 *
 * The SoC power will also be throttled by PSYS if the system power reaches 97%
 * of the charger rating. We prefer throttling the Type-C port over throttling
 * the SoC since this has less user impact.
 *
 * The strategy is to determine what the state of the throttles should be, and
 * to then turn throttles off or on as needed to match this.
 *
 * This function runs on demand, or every 2 ms when the CPU is up, and
 * continually monitors the power usage, applying the throttles when necessary.
 *
 * All measurements are in milliwatts.
 */

/* Throttles we can apply. */
#define THROT_TYPE_C BIT(0)

/* Power gain if Type-C port is limited. */
#define POWER_GAIN_TYPE_C 7500

/*
 * Thresholds at which to start and stop throttling Type-C. Compared against the
 * gap between current power and max power.
 *
 * PSYS will start throttling SoC power when system power reaches 97% of the
 * charger rating (e.g. 63W for a 65W charger), so the low threshold must be
 * at least 2W. We use 4W to ensure we throttle Type-C before we start
 * throttling SoC power.
 *
 * We add 5W of hysteresis to avoid switching frequently during minor power
 * variations.
 */
#define THROT_LOW_THRESHOLD 4000
#define THROT_HIGH_THRESHOLD 9000

/* Power is averaged over 20 ms, with a reading every 2 ms.  */
#define POWER_DELAY_MS 2
#define POWER_READINGS (20 / POWER_DELAY_MS)

static const struct adc_dt_spec adc_ppvar_pwr_in_imon =
	ADC_DT_SPEC_GET_BY_NAME(DT_PATH(zephyr_user), ppvar_pwr_in_imon);

static void board_adc_init_device(void)
{
	adc_channel_setup_dt(&adc_ppvar_pwr_in_imon);
}
DECLARE_HOOK(HOOK_INIT, board_adc_init_device, HOOK_PRIO_INIT_ADC);

test_mockable_static int adc_read_ppvar_pwr_in_imon(void)
{
	int ret = 0, rv;
	struct adc_sequence seq = {
		.options = NULL,
		.channels = BIT(adc_ppvar_pwr_in_imon.channel_id),
		.buffer = &ret,
		.buffer_size = sizeof(ret),
		.resolution = CONFIG_PLATFORM_EC_ADC_RESOLUTION,
		.oversampling = CONFIG_PLATFORM_EC_ADC_OVERSAMPLING,
		.calibrate = false,
	};

	rv = adc_read(adc_ppvar_pwr_in_imon.dev, &seq);
	if (rv) {
		LOG_INF("ADC_PPVAR_PWR_IN_IMON read failed! ");
		return rv;
	}

	adc_raw_to_millivolts(adc_ref_internal(adc_ppvar_pwr_in_imon.dev),
			      ADC_GAIN_1, CONFIG_PLATFORM_EC_ADC_RESOLUTION,
			      &ret);
	/*
	 * factor_mul = ADC_MAX_MVOLT * 2 = 3000 * 2
	 * factor_div = ADC_READ_MAX + 1 = 1023 + 1
	 */
	ret = (ret * 3000 * 2) / (1023 + 1);
	return ret;
}

static void power_monitor(void);
DECLARE_DEFERRED(power_monitor);

static void power_monitor(void)
{
	static uint32_t current_state;
	static uint32_t history[POWER_READINGS];
	static uint8_t index;
	int32_t delay;
	uint32_t new_state = 0, diff;

	/* If CPU is off or suspended, no need to throttle or restrict power. */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF | CHIPSET_STATE_SUSPEND)) {
		/* Slow down monitoring, assume no throttling required. */
		delay = 20 * USEC_PER_MSEC;

		/*
		 * Clear the first entry of the power table so that
		 * it is re-initilalised when the CPU starts.
		 */
		history[0] = 0;
	} else {
		int32_t charger_mw;

		delay = POWER_DELAY_MS * USEC_PER_MSEC;

		/* Get current charger limit. */
		charger_mw = charge_manager_get_power_limit_uw() / 1000;

		if (charger_mw == 0) {
			/*
			 * If unknown, e.g. charge manager not initialised yet,
			 * don't change the throttles.
			 */
			new_state = current_state;
		} else {
			int32_t gap, total, power;
			int i;

			/* Read power usage. */
			power = (charge_manager_get_charger_voltage() *
				 adc_read_ppvar_pwr_in_imon()) /
				1000;

			/* Init power table. */
			if (history[0] == 0) {
				for (i = 0; i < POWER_READINGS; i++) {
					history[i] = power;
				}
			}

			/* Update power readings and calculate the average. */
			history[index] = power;
			index = (index + 1) % POWER_READINGS;
			total = 0;
			for (i = 0; i < POWER_READINGS; i++) {
				total += history[i];
			}
			power = total / POWER_READINGS;

			/* Calculate the gap. */
			gap = charger_mw - power;

			/*
			 * If the Type-C port is sourcing power, check whether
			 * it should be throttled.
			 */
			bool throt_type_c = false;

			if (ppc_is_sourcing_vbus(0)) {
				if (current_state & THROT_TYPE_C) {
					/*
					 * Stop throttling if the gap without
					 * throttling would be greater than the
					 * high threshold.
					 */
					throt_type_c = gap - POWER_GAIN_TYPE_C <
						       THROT_HIGH_THRESHOLD;
				} else {
					/*
					 * Start throttling if the gap is less
					 * than the low threshold.
					 */
					throt_type_c = gap <
						       THROT_LOW_THRESHOLD;
				}
			}
			if (throt_type_c)
				new_state |= THROT_TYPE_C;
		}
	}

	/* Turn the throttles on or off if they have changed. */
	diff = new_state ^ current_state;
	current_state = new_state;
	if (diff & THROT_TYPE_C) {
		enum tcpc_rp_value rp = (new_state & THROT_TYPE_C) ?
						TYPEC_RP_1A5 :
						TYPEC_RP_3A0;

		LOG_INF("%s: %s throttling Type-C", __func__,
			(new_state & THROT_TYPE_C) ? "start" : "stop");

		ppc_set_vbus_source_current_limit(0, rp);
		tcpm_select_rp_value(0, rp);
		pd_update_contract(0);
	}

	hook_call_deferred(&power_monitor_data, delay);
}

/* Start power monitoring after ADCs have been initialised. */
DECLARE_HOOK(HOOK_INIT, power_monitor, HOOK_PRIO_INIT_ADC + 1);
