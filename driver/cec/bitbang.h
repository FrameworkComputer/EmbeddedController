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
 * @param edge		Edge to trigger on
 * @param timeout	Timeout to program into the capture timer
 */
void cec_tmr_cap_start(enum cec_cap_edge edge, int timeout);

/**
 * Stop the capture timer.
 */
void cec_tmr_cap_stop(void);

/**
 * Return the time measured by the capture timer.
 */
int cec_tmr_cap_get(void);

/**
 * ITE-specific callback to record the interrupt time.
 */
__override_proto void cec_update_interrupt_time(void);

/**
 * Called when a transfer is initiated from the host. Should trigger an
 * interrupt which then calls cec_event_tx(). It must be called from interrupt
 * context since the CEC state machine relies on the fact that the state is only
 * modified from interrupt context for synchronisation.
 */
void cec_trigger_send(void);

/**
 * Enable timers used for CEC.
 */
void cec_enable_timer(void);

/**
 * Disable timers used for CEC.
 */
void cec_disable_timer(void);

/**
 * Initialise timers used for CEC.
 */
void cec_init_timer(void);

/**
 * Event for timeout.
 */
void cec_event_timeout(void);

/**
 * Event for capture edge.
 */
void cec_event_cap(void);

/**
 * Event for transfer from host.
 */
void cec_event_tx(void);

/**
 * Interrupt handler for rising and falling edges on the CEC line.
 */
void cec_gpio_interrupt(enum gpio_signal signal);

#endif /* __CROS_EC_DRIVER_CEC_BITBANG_H */
