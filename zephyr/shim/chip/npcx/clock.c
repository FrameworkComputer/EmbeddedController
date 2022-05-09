/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/npcx_clock.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <soc.h>
#include <zephyr/zephyr.h>

#include "clock_chip.h"
#include "module_id.h"

LOG_MODULE_REGISTER(shim_clock, LOG_LEVEL_ERR);

#define CDCG_NODE		DT_INST(0, nuvoton_npcx_pcc)
#define HAL_CDCG_REG_BASE_ADDR \
			((struct cdcg_reg *)DT_REG_ADDR_BY_IDX(CDCG_NODE, 1))

int clock_get_freq(void)
{
	const struct device *clk_dev = DEVICE_DT_GET(NPCX_CLK_CTRL_NODE);
	const struct npcx_clk_cfg clk_cfg = {
		.bus = NPCX_CLOCK_BUS_CORE,
	};
	uint32_t rate;

	if (clock_control_get_rate(clk_dev, (clock_control_subsys_t *)&clk_cfg,
				   &rate) != 0) {
		LOG_ERR("Get %s clock rate error", clk_dev->name);
		return -EIO;
	}

	return rate;
}

void clock_turbo(void)
{
	struct cdcg_reg *const cdcg_base = HAL_CDCG_REG_BASE_ADDR;

	/* For NPCX7:
	 * Increase CORE_CLK (CPU) as the same as OSC_CLK. Since
	 * CORE_CLK > 66MHz, we also need to set AHB6DIV and FIUDIV as 1.
	 */
	cdcg_base->HFCGP = 0x01;
	cdcg_base->HFCBCD = BIT(4);
}

void clock_normal(void)
{
	struct cdcg_reg *const cdcg_base = HAL_CDCG_REG_BASE_ADDR;

	cdcg_base->HFCGP = ((FPRED_VAL << 4) | AHB6DIV_VAL);
	cdcg_base->HFCBCD  = (FIUDIV_VAL << 4);
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
