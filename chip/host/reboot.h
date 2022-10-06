/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Emulator self-reboot procedure */

#ifndef __CROS_EC_REBOOT_H
#define __CROS_EC_REBOOT_H

#include <stdnoreturn.h>

#ifndef TEST_FUZZ
noreturn
#endif
	void
	emulator_reboot(void);

#endif
