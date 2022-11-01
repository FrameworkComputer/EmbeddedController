/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 *@brief Provides implementations of syscalls needed by libc. The newlib
 * documentation provides a list of the required syscalls:
 * https://sourceware.org/newlib/libc.html#Syscalls and minimal implementations
 * can be found in newlib's "nosys" library:
 * https://sourceware.org/git/?p=newlib-cygwin.git;a=tree;f=libgloss/libnosys.
 */

#include "panic.h"
#include "software_panic.h"
#include "task.h"
#include "uart.h"

/**
 * Reboot the system.
 *
 * This function is called from libc functions such as abort() or exit().
 *
 * @param rc exit code
 */
void _exit(int rc)
{
	panic_printf("%s called with rc: %d\n", __func__, rc);
	software_panic(PANIC_SW_EXIT, task_get_current());
}

/**
 * Write to the UART.
 *
 * This function is called from libc functions such as printf().
 *
 * @param fd  ignored
 * @param buf buffer to write
 * @param len number of bytes in @buf to write
 * @return number of bytes successfully written
 */
int _write(int fd, char *buf, int len)
{
	return uart_put(buf, len);
}
