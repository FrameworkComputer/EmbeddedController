/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Event handling in MKBP keyboard protocol
 */

#ifndef __CROS_EC_MKBP_EVENT_H
#define __CROS_EC_MKBP_EVENT_H

/*
 * Sends an event to the AP.
 *
 * When this is called, the event data must be ready for query.  Otherwise,
 * when the AP queries the event, an error is returned and the event is lost.
 *
 * @param event_type  One of EC_MKBP_EVENT_*.
 */
void mkbp_send_event(uint8_t event_type);

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

#define DECLARE_EVENT_SOURCE(type, func)                   \
	const struct mkbp_event_source __evt_src_##type \
	__attribute__((section(".rodata.evtsrcs")))        \
		 = {type, func}

#endif  /* __CROS_EC_MKBP_EVENT_H */
