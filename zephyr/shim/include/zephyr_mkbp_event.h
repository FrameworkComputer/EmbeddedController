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

const struct mkbp_event_source *zephyr_find_mkbp_event_source(
		uint8_t event_type);

/**
 * See include/mkbp_event.h for documentation.
 */
#define DECLARE_EVENT_SOURCE(_type, _func)                                     \
	STRUCT_SECTION_ITERABLE(mkbp_event_source, _cros_evtsrc_##_func) = {   \
		.event_type = _type,                                           \
		.get_data = _func,                                             \
	}
