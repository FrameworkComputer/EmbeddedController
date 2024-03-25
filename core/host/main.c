/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Entry point of unit test executable */

#include "console.h"
#include "flash.h"
#include "hooks.h"
#include "host_task.h"
#include "keyboard_scan.h"
#include "stack_trace.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "uart.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_SYSTEM, outstr)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

const char *__prog_name;

const char *__get_prog_name(void)
{
	return __prog_name;
}

static int test_main(void)
{
	/*
	 * In order to properly service IRQs before task switching is enabled,
	 * we must set up our signal handler for the main thread.
	 */
	task_register_interrupt();

	task_register_tracedump();

	register_test_end_hook();

	crec_flash_pre_init();
	system_pre_init();
	system_common_pre_init();

	test_init();

	timer_init();

	hook_notify(HOOK_INIT_EARLY);

#ifdef HAS_TASK_KEYSCAN
	keyboard_scan_init();
#endif
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

#ifdef TEST_FUZZ
/*
 * Fuzzing tests need to start the main function in a thread, so that
 * LLVMFuzzerTestOneInput can run freely.
 */
void *_main_thread(void *a)
{
	test_main();
	return NULL;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static int initialized;
	static pthread_t main_t;
	/*
	 * We lose the program name as LLVM fuzzer takes over main function:
	 * make up one.
	 */
	static const char *name = STRINGIFY(PROJECT) ".exe";

	if (!initialized) {
		__prog_name = name;
		pthread_create(&main_t, NULL, _main_thread, NULL);
		initialized = 1;
		/* We can't sleep yet, busy loop waiting for tasks to start. */
		wait_for_task_started_nosleep();
		/* Let tasks settle. */
		msleep(50 * MSEC);
	}

	return test_fuzz_one_input(data, size);
}
#else
int main(int argc, char **argv)
{
	__prog_name = argv[0];
	return test_main();
}
#endif
