/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The cmd_c_to_taskinfo will compile this file with different
 * section definitions to export different tasklists.
 */

#include <stdint.h>

#include "config.h"
#include "task_id.h"

#ifdef SECTION_IS_RO
#define GET_TASKINFOS_FUNC get_ro_taskinfos
#elif defined(SECTION_IS_RW)
#define GET_TASKINFOS_FUNC get_rw_taskinfos
#else
#error "Current section (RO/RW) is not defined."
#endif

struct taskinfo {
	char *name;
	char *routine;
	uint32_t stack_size;
};

#define TASK(n, r, d, s, ...)  {	\
	.name = #n,			\
	.routine = #r,			\
	.stack_size = s,		\
},
static const struct taskinfo taskinfos[] = {
	CONFIG_TASK_LIST
};
#undef TASK

uint32_t GET_TASKINFOS_FUNC(const struct taskinfo **infos)
{
	*infos = taskinfos;
	/* Calculate the number of tasks */
	return sizeof(taskinfos) / sizeof(*taskinfos);
}
