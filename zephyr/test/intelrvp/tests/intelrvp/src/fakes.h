/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_INTELRVP_SRC_FAKES_H
#define ZEPHYR_TEST_INTELRVP_SRC_FAKES_H

#include "ccgxxf.h"
#include "ioexpander.h"
#include "it8801.h"
#include "lid_switch.h"
#include "nct38xx.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DECLARE_FAKE_VOID_FUNC(nct38xx_reset_notify, int);
DECLARE_FAKE_VALUE_FUNC(int, ccgxxf_reset, int);
DECLARE_FAKE_VOID_FUNC(io_expander_it8801_interrupt, enum gpio_signal);

#if defined(CONFIG_TEST_PROJECT_MTLRVPP_NPCX) ||     \
	defined(CONFIG_TEST_PROJECT_MTLRVPP_MCHP) || \
	defined(CONFIG_TEST_PROJECT_MTLRVPP_COMMON)
DECLARE_FAKE_VOID_FUNC(lid_interrupt, enum gpio_signal);
DECLARE_FAKE_VALUE_FUNC(int, ioex_init, int);
#endif /* CONFIG_TEST_PROJECT_MTLRVPP */

#if defined(CONFIG_TEST_PROJECT_PTLRVP_MCHP)
DECLARE_FAKE_VALUE_FUNC(int, clock_get_freq);
DECLARE_FAKE_VOID_FUNC(keyboard_raw_drive_column, int);
DECLARE_FAKE_VALUE_FUNC(int, keyboard_raw_read_rows);

#if defined(CONFIG_AP_PWRSEQ_S0IX)
DECLARE_FAKE_VALUE_FUNC(int, x86_non_dsx_mtl_s0ix_run, void *);
#endif /* CONFIG_AP_PWRSEQ_S0IX */
#endif /* CONFIG_TEST_PROJECT_PTLRVP_MCHP */

#endif /* ZEPHYR_TEST_INTELRVP_SRC_FAKES_H */
