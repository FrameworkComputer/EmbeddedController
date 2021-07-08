/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mkbp_event.h"

const struct mkbp_event_source *zephyr_find_mkbp_event_source(uint8_t type)
{
	STRUCT_SECTION_FOREACH(mkbp_event_source, evtsrc) {
		if (evtsrc->event_type == type)
			return evtsrc;
	}

	return NULL;
}
