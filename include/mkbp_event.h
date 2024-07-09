/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Event handling in MKBP keyboard protocol
 */

#ifndef __CROS_EC_MKBP_EVENT_H
#define __CROS_EC_MKBP_EVENT_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * Communicate an MKBP event to the AP via custom method.
 *
 * This can be used if a board has a custom method.
 */
int mkbp_set_host_active_via_custom(int active, uint32_t *timestamp);

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

#ifdef CONFIG_PLATFORM_EC_MKBP_EVENT
#include "zephyr_mkbp_event.h"
#else
#define DECLARE_EVENT_SOURCE(type, func)                            \
	const struct mkbp_event_source __keep __no_sanitize_address \
		_evt_src_##type                                     \
		__attribute__((section(".rodata.evtsrcs"))) = { type, func }
#endif

#ifdef TEST_BUILD
/* Allow directly raising events in unit tests */
void activate_mkbp_with_events(uint32_t events_to_add);

/**
 * @brief Force the event bits to zero, causing the event handling code to
 *        believe there are no pending events to service. This has no effect on
 *        any event sources' internal queues or logic.
 */
__test_only void mkbp_event_clear_all(void);
#endif /* TEST_BUILD */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MKBP_EVENT_H */
