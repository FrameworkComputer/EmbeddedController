/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fakes.h"

/* These macros do not export fake resets. Avoid using these fakes in test
 * sources */
DEFINE_FAKE_VOID_FUNC(nct38xx_reset_notify, int);
DEFINE_FAKE_VALUE_FUNC(int, ccgxxf_reset, int);
DEFINE_FAKE_VOID_FUNC(io_expander_it8801_interrupt, enum gpio_signal);

#if defined(CONFIG_TEST_PROJECT_MTLRVPP_NPCX) ||     \
	defined(CONFIG_TEST_PROJECT_MTLRVPP_MCHP) || \
	defined(CONFIG_TEST_PROJECT_MTLRVPP_COMMON)
DEFINE_FAKE_VOID_FUNC(lid_interrupt, enum gpio_signal);
DEFINE_FAKE_VALUE_FUNC(int, ioex_init, int);
#endif

#if defined(CONFIG_TEST_PROJECT_PTLRVP_MCHP)
DEFINE_FAKE_VALUE_FUNC(int, clock_get_freq);
DEFINE_FAKE_VOID_FUNC(keyboard_raw_drive_column, int);
DEFINE_FAKE_VALUE_FUNC(int, keyboard_raw_read_rows);

#if defined(CONFIG_AP_PWRSEQ_S0IX)
DEFINE_FAKE_VALUE_FUNC(int, x86_non_dsx_mtl_s0ix_run, void *);
#endif /* CONFIG_AP_PWRSEQ_S0IX */
#endif /* CONFIG_TEST_PROJECT_PTLRVP_MCHP */
