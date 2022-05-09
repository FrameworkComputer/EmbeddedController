/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <soc.h>
#include <zephyr/zephyr.h>
#include <soc/ite_it8xxx2/reg_def_cros.h>
#include <zephyr/sys/util.h>

#include "module_id.h"

LOG_MODULE_REGISTER(shim_clock, LOG_LEVEL_ERR);

#define ECPM_NODE		DT_INST(0, ite_it8xxx2_ecpm)
#define HAL_ECPM_REG_BASE_ADDR	\
			((struct ecpm_reg *)DT_REG_ADDR_BY_IDX(ECPM_NODE, 0))
#define PLLFREQ_MASK		0xf

static const int pll_reg_to_freq[8] = {
	MHZ(8),
	MHZ(16),
	MHZ(24),
	MHZ(32),
	MHZ(48),
	MHZ(64),
	MHZ(72),
	MHZ(96)
};

int clock_get_freq(void)
{
	struct ecpm_reg *const ecpm_base = HAL_ECPM_REG_BASE_ADDR;
	int reg_val = ecpm_base->ECPM_PLLFREQ & PLLFREQ_MASK;

	return pll_reg_to_freq[reg_val];
}
