/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <drivers/clock_control.h>
#include <dt-bindings/clock/mchp_xec_pcr.h>
#include <kernel.h>
#include <logging/log.h>
#include <soc.h>
#include <zephyr.h>

#include "clock_chip.h"
#include "module_id.h"

LOG_MODULE_REGISTER(shim_clock, LOG_LEVEL_ERR);

#define PCR_NODE		DT_INST(0, microchip_xec_pcr)
#define HAL_PCR_REG_BASE_ADDR \
			((struct pcr_regs *)DT_REG_ADDR_BY_IDX(PCR_NODE, 0))

int clock_get_freq(void)
{
	const struct device *clk_dev = DEVICE_DT_GET(PCR_NODE);
	uint32_t bus = MCHP_XEC_PCR_CLK_CPU;
	uint32_t rate;

	if (clock_control_get_rate(clk_dev, (clock_control_subsys_t)bus,
				   &rate) != 0) {
		LOG_ERR("Get %s clock rate error", clk_dev->name);
		return -EIO;
	}

	return rate;
}

void clock_turbo(void)
{
	struct pcr_regs *const pcr = HAL_PCR_REG_BASE_ADDR;

	pcr->PROC_CLK_CTRL = 1;
	__DSB();
	__ISB();
	__NOP();
	__NOP();
	__NOP();
	__NOP();
	__NOP();
	__NOP();
}

void clock_normal(void)
{
	struct pcr_regs *const pcr = HAL_PCR_REG_BASE_ADDR;

	pcr->PROC_CLK_CTRL = 4;
	__DSB();
	__ISB();
	__NOP();
	__NOP();
	__NOP();
	__NOP();
	__NOP();
	__NOP();
}

void clock_enable_module(enum module_id module, int enable)
{
	/* Assume we have a single task using MODULE_FAST_CPU */
	if (module == MODULE_FAST_CPU) {
		if (enable)
			clock_turbo();
		else
			clock_normal();
	}
}
