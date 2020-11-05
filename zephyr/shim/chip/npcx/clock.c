/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <device.h>
#include <drivers/clock_control.h>
#include <dt-bindings/clock/npcx_clock.h>
#include <kernel.h>
#include <logging/log.h>
#include <soc.h>
#include <zephyr.h>

LOG_MODULE_REGISTER(shim_clock, LOG_LEVEL_ERR);

int clock_get_freq(void)
{
	const struct device *clk_dev = device_get_binding(NPCX_CLK_CTRL_NAME);
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
