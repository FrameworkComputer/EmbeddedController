/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cec.h"
#include "cec_bitbang_chip.h"
#include "console.h"
#include "driver/cec/bitbang.h"
#include "gpio.h"
#include "hwtimer_chip.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define CPRINTS(format, args...) cprints(CC_CEC, format, ##args)

#ifdef CONFIG_CEC_DEBUG
#define DEBUG_CPRINTF(format, args...) cprintf(CC_CEC, format, ##args)
#define DEBUG_CPRINTS(format, args...) cprints(CC_CEC, format, ##args)
#else
#define DEBUG_CPRINTF(...)
#define DEBUG_CPRINTS(...)
#endif

/* Timestamp when the most recent interrupt occurred */
static timestamp_t interrupt_time;

/* Timestamp when the second most recent interrupt occurred */
static timestamp_t prev_interrupt_time;

/* Flag set when a transfer is initiated from the AP */
static bool transfer_initiated;

/* The capture edge we're waiting for */
static enum cec_cap_edge expected_cap_edge;

static int port_from_timer(enum ext_timer_sel ext_timer)
{
	int port;
	const struct bitbang_cec_config *drv_config;

	for (port = 0; port < CEC_PORT_COUNT; port++) {
		if (cec_config[port].drv == &bitbang_cec_drv) {
			drv_config = cec_config[port].drv_config;
			if (drv_config->timer == ext_timer)
				return port;
		}
	}

	/*
	 * If we don't find a match, return 0. The only way for this to happen
	 * is a configuration error, e.g. an incorrect timer is specified in
	 * board.c, and we assume static configuration is correct to improve
	 * performance.
	 */
	return 0;
}

static int port_from_gpio_in(enum gpio_signal signal)
{
	int port;
	const struct bitbang_cec_config *drv_config;

	for (port = 0; port < CEC_PORT_COUNT; port++) {
		if (cec_config[port].drv == &bitbang_cec_drv) {
			drv_config = cec_config[port].drv_config;
			if (drv_config->gpio_in == signal)
				return port;
		}
	}

	/*
	 * If we don't find a match, return 0. The only way for this to happen
	 * is a configuration error, e.g. an incorrect pin is mapped to
	 * cec_gpio_interrupt in gpio.inc, and we assume static configuration
	 * is correct to improve performance.
	 */
	return 0;
}

/*
 * ITE doesn't have a capture timer, so we use a countdown timer for timeout
 * events combined with a GPIO interrupt for capture events.
 */
void cec_tmr_cap_start(int port, enum cec_cap_edge edge, int timeout)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	expected_cap_edge = edge;

	if (timeout > 0) {
		/*
		 * Take into account the delay from when the interrupt occurs to
		 * when we actually get here. Since the timing is done in
		 * software, there is an additional unknown delay from when the
		 * interrupt occurs to when the ISR starts. Empirically, this
		 * seems to be about 100 us, so account for this too.
		 */
		int delay = CEC_US_TO_TICKS(get_time().val -
					    interrupt_time.val + 100);
		int timer_count = timeout - delay;

		/*
		 * Handle the case where the delay is greater than the timeout.
		 * This should never actually happen for typical delay and
		 * timeout values.
		 */
		if (timer_count < 0) {
			timer_count = 0;
			CPRINTS("CEC%d warning: timer_count < 0", port);
		}

		/* Start the timer and enable the timer interrupt */
		ext_timer_ms(drv_config->timer, CEC_CLOCK_SOURCE, 1, 1,
			     timer_count, 0, 1);
	} else {
		ext_timer_stop(drv_config->timer, 1);
	}
}

void cec_tmr_cap_stop(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	gpio_disable_interrupt(drv_config->gpio_in);
	ext_timer_stop(drv_config->timer, 1);
}

int cec_tmr_cap_get(int port)
{
	return CEC_US_TO_TICKS(interrupt_time.val - prev_interrupt_time.val);
}

/*
 * In most states it83xx keeps gpio interrupts enabled to improve timing (see
 * https://crrev.com/c/4899696). But for the debounce logic to work, gpio
 * interrupts must be disabled, so we disable them when entering the debounce
 * state and re-enable them when leaving the state.
 */
void cec_debounce_enable(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	gpio_disable_interrupt(drv_config->gpio_in);
}

void cec_debounce_disable(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	gpio_enable_interrupt(drv_config->gpio_in);
}

__override void cec_update_interrupt_time(int port)
{
	prev_interrupt_time = interrupt_time;
	interrupt_time = get_time();
}

void cec_ext_timer_interrupt(enum ext_timer_sel ext_timer)
{
	int port = port_from_timer(ext_timer);

	if (transfer_initiated) {
		transfer_initiated = false;
		cec_event_tx(port);
	} else {
		cec_update_interrupt_time(port);
		cec_event_timeout(port);
	}
}

void cec_gpio_interrupt(enum gpio_signal signal)
{
	int port = port_from_gpio_in(signal);
	int level;

	cec_update_interrupt_time(port);

	level = gpio_get_level(signal);
	if (!((expected_cap_edge == CEC_CAP_EDGE_FALLING && level == 0) ||
	      (expected_cap_edge == CEC_CAP_EDGE_RISING && level == 1)))
		return;

	cec_event_cap(port);
}

void cec_trigger_send(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	/* Elevate to interrupt context */
	transfer_initiated = true;
	task_trigger_irq(et_ctrl_regs[drv_config->timer].irq);
}

void cec_enable_timer(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	/*
	 * Enable gpio interrupts. Timer interrupts will be enabled as needed by
	 * cec_tmr_cap_start().
	 */
	gpio_enable_interrupt(drv_config->gpio_in);
}

void cec_disable_timer(int port)
{
	cec_tmr_cap_stop(port);

	interrupt_time.val = 0;
	prev_interrupt_time.val = 0;
}

void cec_init_timer(int port)
{
	const struct bitbang_cec_config *drv_config =
		cec_config[port].drv_config;

	ext_timer_ms(drv_config->timer, CEC_CLOCK_SOURCE, 0, 0, 0, 1, 0);
}
