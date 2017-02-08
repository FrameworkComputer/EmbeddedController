/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Sensor common routines. */

#include "common.h"
#include "console.h"
#include "motion_sense.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_MOTION_SENSE, outstr)
#define CPRINTS(format, args...) cprints(CC_MOTION_SENSE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_MOTION_SENSE, format, ## args)

void sensor_init_done(const struct motion_sensor_t *s, int range)
{
#ifdef CONFIG_CONSOLE_VERBOSE
	CPRINTS("%s: MS Done Init type:0x%X range:%d",
		s->name, s->type, range);
#else
	CPRINTS("%c%d InitDone r:%d", s->name[0], s->type, range);
#endif
}
