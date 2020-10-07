/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator self-reboot procedure */

#include <string.h>
#include <unistd.h>

#include "console.h"
#include "host_test.h"
#include "reboot.h"
#include "test_util.h"

#ifdef TEST_FUZZ
/* reboot breaks fuzzing, let's just not do it. */
void emulator_reboot(void)
{
	ccprints("Emulator would reboot here. Fuzzing: doing nothing.");
}
#else /* !TEST_FUZZ */
noreturn
void emulator_reboot(void)
{
	char *argv[] = {strdup(__get_prog_name()), NULL};
	emulator_flush();
	execv(__get_prog_name(), argv);
	while (1)
		;
}
#endif /* !TEST_FUZZ */
