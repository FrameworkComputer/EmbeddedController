/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Entry point of unit test executable */

#include "hooks.h"
#include "task.h"
#include "timer.h"

int main(void)
{
	timer_init();

	hook_init();

	task_start();

	return 0;
}
