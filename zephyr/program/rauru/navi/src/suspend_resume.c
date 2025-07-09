/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ap_power/ap_power.h"
#include "atomic.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "power.h"
#include "power/mt8186.h"
#include "timer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/gpio/ite-it8xxx2-gpio.h>
#include <zephyr/dt-bindings/interrupt-controller/ite-intc.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/atomic.h>

#include <chip_chipregs.h>

LOG_MODULE_DECLARE(suspend_resume_hack, LOG_LEVEL_INF);

/*
 * This file provides a workaround for an issue on early versions of the
 * Rauru/Navi platform where the AP_IN_SLEEP_L signal, intended to indicate
 * the AP's S3 sleep state, is unreliable.
 *
 * To address this, we utilize the AP/EC SPI CS_L pin as a substitute S3
 * indicator. When the AP enters S3 sleep (by sending the
 * EC_CMD_HOST_SLEEP_EVENT command to the EC), we monitor the CS_L line.
 * A low signal on CS_L indicates that the AP is in S3.  We then set the
 * AP_IN_SLEEP_L pin to OUTPUT_LOW.
 *
 * Upon resuming from S3, the CS_L line goes high. This signals the AP's
 * wake-up, allowing us to accurately detect the AP's power state.
 */

#define PREINIT_VERSION 0xDEADDEAD

static uint32_t board_version = PREINIT_VERSION;
static struct gpio_callback cs_callback;
static const struct gpio_dt_spec cs_gpio =
	GPIO_DT_SPEC_GET(DT_NODELABEL(shi0), cs_gpios);
static void ap_wakeup_isr(const struct device *port, struct gpio_callback *cb,
			  gpio_port_pins_t pins);

/*
 * Enables the interrupt on the CS_L pin. This is done with a deferred call
 * to avoid race conditions during the SPI transaction.
 */
static void enable_cs_interrupt(void)
{
	gpio_add_callback_dt(&cs_gpio, &cs_callback);
	gpio_pin_interrupt_configure_dt(&cs_gpio, GPIO_INT_EDGE_BOTH);

	/* trigger the callback if CS_L is already low at this point */
	if (!gpio_pin_get_dt(&cs_gpio)) {
		ap_wakeup_isr(cs_gpio.port, &cs_callback, cs_gpio.pin);
	}
}
DECLARE_DEFERRED(enable_cs_interrupt);

/*
 * Disables the interrupt on the CS_L pin.
 */
static void disable_cs_interrupt(void)
{
	gpio_remove_callback_dt(&cs_gpio, &cs_callback);
	gpio_pin_interrupt_configure_dt(&cs_gpio, GPIO_INT_EDGE_FALLING);
}

static void ap_wakeup_isr_debounce(void)
{
	const struct gpio_dt_spec *s3_indicator_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);
	int val = gpio_pin_get_dt(&cs_gpio);

	if (val) {
		disable_cs_interrupt();
		gpio_pin_configure_dt(s3_indicator_l, GPIO_INPUT);
	}
}

DECLARE_DEFERRED(ap_wakeup_isr_debounce);

#define AP_WAKEUP_ISR_DEBOUNCE_T (2 * USEC_PER_MSEC)
/*
 * Interrupt handler for the CS_L pin. This function is called when the AP
 * enters or exits S3 sleep.
 */
static void ap_wakeup_isr(const struct device *port, struct gpio_callback *cb,
			  gpio_port_pins_t pins)
{
	const struct gpio_dt_spec *s3_indicator_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);
	int val = gpio_pin_get_dt(&cs_gpio);

	if (val) {
		/*
		 * (b/354870788#comment88) Workaround for the unepxected RTC
		 * wake-up from AP. This This should be reverted once resolved
		 * in the SPM.
		 **/
		hook_call_deferred(&ap_wakeup_isr_debounce_data,
				   AP_WAKEUP_ISR_DEBOUNCE_T);
	} else {
		gpio_pin_configure_dt(s3_indicator_l, GPIO_OUTPUT_LOW);
		hook_call_deferred(&ap_wakeup_isr_debounce_data, -1);
	}
}

/*
 * Event handler for AP power events. This function is called when the AP
 * shuts down or resumes from S3.
 */
static void navi_power_event_handler(struct ap_power_ev_callback *callback,
				     struct ap_power_ev_data data)
{
	const struct gpio_dt_spec *s3_indicator_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);

	switch (data.event) {
	case AP_POWER_SHUTDOWN:
		/* fall-through */
	case AP_POWER_RESUME_INIT:
		disable_cs_interrupt();
		gpio_pin_configure_dt(s3_indicator_l, GPIO_INPUT);
		break;
	default:
		break;
	}
}

/*
 * Initializes the workaround. This function is called during system
 * initialization.
 */
static void init_suspend_resume_workaround(void)
{
	static struct ap_power_ev_callback power_event_cb;

	if (board_version == PREINIT_VERSION) {
		if (cbi_get_board_version(&board_version)) {
			LOG_ERR("Getting board version failed.");
			return;
		}
	}

	/* Workaround not needed for newer boards */
	if (board_version > 1) {
		return;
	}

	LOG_INF("Board version <= 1. Applying suspend/resume workaround.");

	/* Register for AP power events */
	ap_power_ev_init_callback(&power_event_cb, navi_power_event_handler,
				  AP_POWER_RESUME_INIT | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&power_event_cb);

	/* Initialize the CS_L interrupt callback */
	gpio_init_callback(&cs_callback, ap_wakeup_isr, BIT(cs_gpio.pin));
}
DECLARE_HOOK(HOOK_INIT, init_suspend_resume_workaround, HOOK_PRIO_LAST);

__override void board_handle_host_sleep_event(enum host_sleep_event state)
{
	/* Workaround not needed for newer boards */
	if (board_version > 1) {
		return;
	}

	if (state == HOST_SLEEP_EVENT_S3_SUSPEND) {
		/*
		 * Delay 50 ms to enable the IRQ to avoid being triggered by the
		 * ongoing SPI transaction.
		 */
		hook_call_deferred(&enable_cs_interrupt_data,
				   50 * USEC_PER_MSEC);
	}
}

__override void board_handle_sleep_hang(enum sleep_hang_type hang_type)
{
	const struct gpio_dt_spec *s3_indicator_l =
		GPIO_DT_FROM_NODELABEL(gpio_ap_in_sleep_l);

	/* Note the S0IX is not actually S3 on ARM platform */
	if (hang_type == SLEEP_HANG_S0IX_SUSPEND ||
	    hang_type == SLEEP_HANG_S0IX_RESUME) {
		disable_cs_interrupt();
		gpio_pin_configure_dt(s3_indicator_l, GPIO_INPUT);
	} else {
		LOG_WRN("Unhandled hang %x", hang_type);
	}
}
