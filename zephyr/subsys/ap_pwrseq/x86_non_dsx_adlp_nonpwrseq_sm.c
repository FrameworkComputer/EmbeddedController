/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ap_pwrseq/x86_non_dsx_adlp_pwrseq_sm.h>

LOG_MODULE_DECLARE(ap_pwrseq, 4);

__override int all_sys_pwrgd_handler(void)
{
	/* Add signal handling */
	return 0;
}

__override int intel_x86_get_pg_ec_dsw_pwrok(void)
{
	/* Add signal handling */
	return 0;
}
