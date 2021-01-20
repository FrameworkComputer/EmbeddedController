/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(__CROS_EC_MKBP_EVENT_H) || \
	defined(__CROS_EC_ZEPHYR_MKBP_EVENT_H)
#error "This file must only be included from mkbp_event.h. " \
	"Include mkbp_event.h directly"
#endif
#define __CROS_EC_ZEPHYR_MKBP_EVENT_H

/** Node in a list of mkbp event handlers */
struct zshim_mkbp_event_node {
	struct mkbp_event_source evtsrc;
	struct zshim_mkbp_event_node *next;
};

/**
 * Runtime helper for DECLARE_EVENT_SOURCE setup data.
 *
 * @param event_type	event type (EC_MKBP_EVENT_...)
 * @param get_data	event handler for the event type
 * @param entry		pointer to statically allocated zshim_mkbp_event_node
			item
 */
void zshim_setup_mkbp_event(uint8_t event_type, int (*get_data)(uint8_t *data),
			    struct zshim_mkbp_event_node *entry);

const struct mkbp_event_source *zephyr_find_mkbp_event_source(
		uint8_t event_type);

/**
 * See include/mkbp_event.h for documentation.
 */
#define DECLARE_EVENT_SOURCE(type, func) \
	_DECLARE_EVENT_SOURCE_1(type, func, __LINE__)
#define _DECLARE_EVENT_SOURCE_1(type, func, line) \
	_DECLARE_EVENT_SOURCE_2(type, func, line)
#define _DECLARE_EVENT_SOURCE_2(type, func, line)                          \
	static int _setup_mkbp_event_##line(const struct device *unused)   \
	{                                                                  \
		ARG_UNUSED(unused);                                        \
		static struct zshim_mkbp_event_node mkbp_event;            \
		zshim_setup_mkbp_event(type, func, &mkbp_event);           \
		return 0;                                                  \
	}                                                                  \
	SYS_INIT(_setup_mkbp_event_##line, APPLICATION, 1)
