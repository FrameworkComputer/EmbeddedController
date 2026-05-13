/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/init.h>

#include <otp_key.h>

/* Initialize OTP key if not already initialized */
static int initialize_otp(void)
{
	otp_key_provision();

	return 0;
}
SYS_INIT(initialize_otp, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
