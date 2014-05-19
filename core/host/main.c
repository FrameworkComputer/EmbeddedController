/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Entry point of unit test executable */

#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "stack_trace.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "uart.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

const char *__prog_name;

const char *__get_prog_name(void)
{
	return __prog_name;
}

int main(int argc, char **argv)
{
	__prog_name = argv[0];

	task_register_tracedump();

	register_test_end_hook();

	flash_pre_init();
	system_pre_init();
	system_common_pre_init();

	test_init();

	timer_init();
#ifdef HAS_TASK_KEYSCAN
	keyboard_scan_init();
#endif
	hook_init();
	uart_init();

	if (system_jumped_to_this_image()) {
		CPRINTS("Emulator initialized after sysjump");
	} else {
		CPUTS("\n\n--- Emulator initialized after reboot ---\n");
		CPUTS("[Reset cause: ");
		system_print_reset_flags();
		CPUTS("]\n");
	}

	task_start();

	return 0;
}
