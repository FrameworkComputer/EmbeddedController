/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "nsi_host_trampolines.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <unistd.h>

void ztest_printk_stdout(const char *fmt, ...)
{
	char buffer[256];
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = vsnprintf(buffer, sizeof(buffer), fmt, ap);
	va_end(ap);

	nsi_host_write(STDOUT_FILENO, buffer, rc);
}
