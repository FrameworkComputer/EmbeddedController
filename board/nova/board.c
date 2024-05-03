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
#include "driver/als_tcs3400.h"
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

static uint8_t usbc_overcurrent;
static int32_t base_5v_power;

/*
 * Power usage for each port as measured or estimated.
 * Units are milliwatts (5v x ma current)
 */
#define PWR_BASE_LOAD (5 * 1335)
#define PWR_FRONT_HIGH (5 * 1603)
#define PWR_FRONT_LOW (5 * 963)
#define PWR_REAR (5 * 1075)
#define PWR_HDMI (5 * 562)
#define PWR_C_HIGH (5 * 3740)
#define PWR_C_LOW (5 * 2090)
#define PWR_MAX (5 * 10000)

/*
 * Update the 5V power usage, assuming no throttling,
 * and invoke the power monitoring.
 */
static void update_5v_usage(void)
{
	if (!gpio_get_level(GPIO_USB_A0_OC_ODL))
		base_5v_power += PWR_REAR;
	if (!gpio_get_level(GPIO_USB_A1_OC_ODL))
		base_5v_power += PWR_REAR;
	if (!gpio_get_level(GPIO_HDMI_CONN_OC_ODL))
		base_5v_power += PWR_HDMI;
	if (usbc_overcurrent)
		base_5v_power += PWR_C_HIGH;
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

static void board_init(void)
{
	gpio_enable_interrupt(GPIO_HDMI_CONN_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A0_OC_ODL);
	gpio_enable_interrupt(GPIO_USB_A1_OC_ODL);
	gpio_enable_interrupt(GPIO_EC_RGB_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

void board_enable_scaler_rails(int enable)
{
	/*
	 * Toggle scaler power and its downstream USB devices.
	 */
	gpio_set_level(GPIO_EC_SCALER_EN, enable);
	gpio_set_level(GPIO_PWR_CTRL, enable);
	gpio_set_level(GPIO_EC_MX8M_ONOFF, enable);
	gpio_set_level(GPIO_EC_CAM_V3P3_EN, enable);
}

/* Called on AP S5 -> S3 transition */
static void board_chipset_startup(void)
{
	board_enable_scaler_rails(1);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S3 -> S5 transition */
static void board_chipset_shutdown(void)
{
	board_enable_scaler_rails(0);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_chipset_shutdown, HOOK_PRIO_DEFAULT);

/*
 * TPU is turned on in S0, off in S0ix and lower.
 */
static void disable_tpu_power(void)
{
	gpio_set_level(GPIO_PP3300_TPU_EN, 0);
	gpio_set_level(GPIO_EC_IMX8_EN, 0);
}

static void enable_tpu_power(void)
{
	gpio_set_level(GPIO_PP3300_TPU_EN, 1);
	gpio_set_level(GPIO_EC_IMX8_EN, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, disable_tpu_power, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, enable_tpu_power, HOOK_PRIO_DEFAULT);

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
 * The overall goal is to gracefully manage the power demand so that
 * the power budgets are met without letting the system fall into
 * power deficit (perhaps causing a brownout).
 *
 * There are 2 power budgets that need to be managed:
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
 *  - Type A BC1.2 front port restriction (3W)
 *  - Type C PD (throttle to 1.5A if sourcing)
 *  - Turn on PROCHOT, which immediately throttles the CPU.
 *
 *  The first 2 throttles affect both the system power and the 5V rails.
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
#define THROT_TYPE_A BIT(0)
#define THROT_TYPE_C BIT(1)
#define THROT_PROCHOT BIT(2)

/*
 * Power gain if front USB A ports are limited.
 */
#define POWER_GAIN_TYPE_A 3200
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
	int32_t delay;
	uint32_t new_state = 0, diff;
	int32_t headroom_5v = PWR_MAX - base_5v_power;

	/*
	 * If CPU is off or suspended, no need to throttle
	 * or restrict power.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF | CHIPSET_STATE_SUSPEND)) {
		/*
		 * Slow down monitoring, assume no throttling required.
		 */
		delay = 20 * MSEC;
	} else {
		delay = POWER_DELAY_MS * MSEC;
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
	if (headroom_5v < 0) {
		/*
		 * Check whether type C is not throttled,
		 * and is not overcurrent.
		 */
		if (!((new_state & THROT_TYPE_C) || usbc_overcurrent)) {
			/*
			 * [1] Type C not in overcurrent, throttle it.
			 */
			headroom_5v += PWR_C_HIGH - PWR_C_LOW;
			new_state |= THROT_TYPE_C;
		}
		/*
		 * [2] If type A not already throttled, and power still
		 * needed, limit type A.
		 */
		if (!(new_state & THROT_TYPE_A) && headroom_5v < 0) {
			headroom_5v += PWR_FRONT_HIGH - PWR_FRONT_LOW;
			new_state |= THROT_TYPE_A;
		}
		/*
		 * [3] If still under-budget, limit type C.
		 * No need to check if it is already throttled or not.
		 */
		if (headroom_5v < 0)
			new_state |= THROT_TYPE_C;
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
	if (diff & THROT_TYPE_A) {
		int typea_bc = (new_state & THROT_TYPE_A) ? 1 : 0;

		gpio_set_level(GPIO_USB_A_LOW_PWR_OD, typea_bc);
	}
	hook_call_deferred(&power_monitor_data, delay);
}
