/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Event handling in MKBP keyboard protocol
 */

#ifndef __CROS_EC_MKBP_EVENT_H
#define __CROS_EC_MKBP_EVENT_H

/*
 * Last time the host received an interrupt.
 *
 * Retrieved via __hw_clock_source_read() as close as possible
 * to the interrupt source. Intended to be virtually the same time the
 * first line of the AP hard irq for the EC interrupt.
 */
extern uint32_t mkbp_last_event_time;

/*
 * Sends an event to the AP.
 *
 * When this is called, the event data must be ready for query.  Otherwise,
 * when the AP queries the event, an error is returned and the event is lost.
 *
 * @param event_type  One of EC_MKBP_EVENT_*.
 * @return   True if event succeeded to generate host interrupt.
 */
int mkbp_send_event(uint8_t event_type);

/*
 * Set MKBP active event status on the AP.
 *
 * This communicates to the AP whether an MKBP event is currently available
 * for processing. It is used by mkbp_send_event().
 *
 * The default implementation in mkbp_event.c has weak linkage and can be
 * overridden by individual boards depending on their hardware configuration.
 *
 * @param active  1 if there is an event, 0 otherwise
 */
void mkbp_set_host_active(int active);

/*
 * Communicate an MKBP event to the host via a dedicated GPIO pin.
 *
 * This can be used if the board schematic has a pin reserved for this purpose.
 */
void mkbp_set_host_active_via_gpio(int active);

/*
 * Communicate an MKBP event to the AP via EC_HOST_EVENT.
 *
 * This can be used without a dedicated interrupt pin configured. It is the
 * default behavior of mkbp_set_host_active when CONFIG_MKBP_USE_HOST_EVENT
 * is defined in board.h.
 */
void mkbp_set_host_active_via_event(int active);

/*
 * The struct to store the event source definition.  The get_data routine is
 * responsible for returning the event data when queried by the AP.  The
 * parameter 'data' points to where the event data needs to be stored, and
 * the size of the event data should be returned.
 */
struct mkbp_event_source {
	uint8_t event_type;
	int (*get_data)(uint8_t *data);
};

#define DECLARE_EVENT_SOURCE(type, func)                       \
	const struct mkbp_event_source __keep		       \
	__no_sanitize_address _evt_src_##type		       \
	__attribute__((section(".rodata.evtsrcs")))            \
		 = {type, func}

#endif  /* __CROS_EC_MKBP_EVENT_H */
