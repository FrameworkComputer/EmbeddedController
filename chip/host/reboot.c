/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator self-reboot procedure */

#include <string.h>
#include <unistd.h>

#include "host_test.h"
#include "reboot.h"

void emulator_reboot(void)
{
	char *argv[] = {strdup(__get_prog_name()), NULL};
	execv(__get_prog_name(), argv);
}
