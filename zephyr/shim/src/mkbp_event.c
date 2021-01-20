/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mkbp_event.h"

static struct zshim_mkbp_event_node *mkbp_event_head;

void zshim_setup_mkbp_event(uint8_t event_type, int (*get_data)(uint8_t *data),
                            struct zshim_mkbp_event_node *entry)
{

	entry->evtsrc.event_type = event_type;
	entry->evtsrc.get_data = get_data;
	entry->next = mkbp_event_head;

	mkbp_event_head = entry;
}

const struct mkbp_event_source *zephyr_find_mkbp_event_source(uint8_t type)
{
	struct zshim_mkbp_event_node *p;

	for (p = mkbp_event_head; p != NULL; p = p->next) {
		if (p->evtsrc.event_type == type)
			return &p->evtsrc;
	}

	return NULL;
}
