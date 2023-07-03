/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_DRIVER_CEC_BITBANG_H
#define __CROS_EC_DRIVER_CEC_BITBANG_H

/* Edge to trigger capture timer interrupt on */
enum cec_cap_edge {
	CEC_CAP_EDGE_NONE,
	CEC_CAP_EDGE_FALLING,
	CEC_CAP_EDGE_RISING
};

extern const struct cec_drv bitbang_cec_drv;

/**
 * Start the capture timer. An interrupt will be triggered when either a capture
 * edge or a timeout occurs.
 * If edge is NONE, disable the capture interrupt and wait for a timeout only.
 * If timeout is 0, disable the timeout interrupt and wait for a capture event
 * only.
 *
 * @param port		CEC port to start capture timer for
 * @param edge		Edge to trigger on
 * @param timeout	Timeout to program into the capture timer
 */
void cec_tmr_cap_start(int port, enum cec_cap_edge edge, int timeout);

/**
 * Stop the capture timer.
 *
 * @param port		CEC port to stop capture timer for
 */
void cec_tmr_cap_stop(int port);

/**
 * Return the time measured by the capture timer.
 *
 * @param port		CEC port to get capture timer value for
 */
int cec_tmr_cap_get(int port);

/**
 * ITE-specific callback to record the interrupt time.
 *
 * @param port		CEC port where the interrupt occurred.
 */
__override_proto void cec_update_interrupt_time(int port);

/**
 * Called when a transfer is initiated from the host. Should trigger an
 * interrupt which then calls cec_event_tx(). It must be called from interrupt
 * context since the CEC state machine relies on the fact that the state is only
 * modified from interrupt context for synchronisation.
 *
 * @param port		CEC port the transfer is initiated on.
 */
void cec_trigger_send(int port);

/**
 * Enable timers used for CEC.
 *
 * @param port		CEC port to enable.
 */
void cec_enable_timer(int port);

/**
 * Disable timers used for CEC.
 *
 * @param port		CEC port to disable.
 */
void cec_disable_timer(int port);

/**
 * Initialise timers used for CEC.
 *
 * @param port		CEC port to initialise.
 */
void cec_init_timer(int port);

/**
 * Event for timeout.
 *
 * @param port		CEC port the event occurred on.
 */
void cec_event_timeout(int port);

/**
 * Event for capture edge.
 *
 * @param port		CEC port the event occurred on.
 */
void cec_event_cap(int port);

/**
 * Event for transfer from host.
 *
 * @param port		CEC port the event occurred on.
 */
void cec_event_tx(int port);

/**
 * Interrupt handler for rising and falling edges on the CEC line.
 */
void cec_gpio_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_DRIVER_CEC_BITBANG_H */
