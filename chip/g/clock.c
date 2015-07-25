/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "clock.h"
#include "registers.h"
#include "pmu.h"

void clock_init(void)
{
	pmu_clock_en(PERIPH_PERI0);
	pmu_clock_en(PERIPH_TIMEHS0);
	pmu_clock_en(PERIPH_TIMEHS1);
	pmu_clock_switch_xo();
}

void clock_enable_module(enum module_id module, int enable)
{
	pmu_clock_func clock_func;
	clock_func = (enable) ?  pmu_clock_en : pmu_clock_dis;

	switch (module) {
	case MODULE_UART:
		clock_func(PERIPH_UART0);
		break;
	case MODULE_I2C:
		clock_func(PERIPH_I2C0);
		clock_func(PERIPH_I2C1);
		break;
	case MODULE_SPI_FLASH:
	case MODULE_SPI_MASTER:
		clock_func(PERIPH_SPI);
		break;
	case MODULE_SPI:
		clock_func(PERIPH_SPS);
		break;
	case MODULE_USB:
		clock_func(PERIPH_USB0);
		clock_func(PERIPH_USB0_USB_PHY);
		pmu_enable_clock_doubler();
		break;
	case MODULE_PMU:
		clock_func(PERIPH_PMU);
		break;
	default:
		break;
	}
	return;
}
