/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "board.h"
#include "common.h"
#include "console.h"
#include "debug.h"
#include "hooks.h"
#include "registers.h"
#include "util.h"
#include "usb_pd.h"

/* ------------------------- Power supply control ------------------------ */

/* GPIO level setting helpers through BSRR register */
#define GPIO_SET(n)   (1 << (n))
#define GPIO_RESET(n) (1 << ((n) + 16))

/* Output voltage selection */
enum volt {
	VO_5V  = GPIO_RESET(13) | GPIO_RESET(14),
	VO_12V = GPIO_SET(13)   | GPIO_RESET(14),
	VO_13V = GPIO_RESET(13) | GPIO_SET(14),
	VO_20V = GPIO_SET(13)   | GPIO_SET(14),
};

static inline void set_output_voltage(enum volt v)
{
	/* set voltage_select on PA13/PA14 */
	STM32_GPIO_BSRR(GPIO_A) = v;
}

static inline void output_enable(void)
{
	/* GPF0 (FET driver shutdown) = 0 */
	STM32_GPIO_BSRR(GPIO_F) = GPIO_RESET(0);
}

static inline void output_disable(void)
{
	/* GPF0 (FET driver shutdown) = 1 */
	STM32_GPIO_BSRR(GPIO_F) = GPIO_SET(0);
}

/* ----------------------- USB Power delivery policy ---------------------- */

/* Power Delivery Objects */
const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   500, PDO_FIXED_EXTERNAL),
		PDO_FIXED(5000,  3000, 0),
		PDO_FIXED(12000, 3000, 0),
		PDO_FIXED(20000, 2000, 0),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

/* PDO voltages (should match the table above) */
static enum volt voltages[ARRAY_SIZE(pd_src_pdo)] = {
	VO_5V,
	VO_5V,
	VO_12V,
	VO_20V,
};

int pd_request_voltage(uint32_t rdo)
{
	int op_ma = rdo & 0x3FF;
	int max_ma = (rdo >> 10) & 0x3FF;
	int idx = rdo >> 28;
	uint32_t pdo;
	uint32_t pdo_ma;

	if (!idx || idx > pd_src_pdo_cnt)
		return EC_ERROR_INVAL; /* Invalid index */

	/* check current ... */
	pdo = pd_src_pdo[idx - 1];
	pdo_ma = (pdo & 0x3ff);
	if (op_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much op current */
	if (max_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much max current */

	debug_printf("Switch to %d V %d mA (for %d/%d mA)\n",
		     ((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		     ((rdo >> 10) & 0x3ff) * 10, (rdo & 0x3ff) * 10);

	output_disable();
	/* TODO discharge ? */
	set_output_voltage(voltages[idx-1]);

	return EC_SUCCESS;
}

int pd_set_power_supply_ready(void)
{
	output_enable();
	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(void)
{
	output_disable();
	/* TODO discharge ? */
	set_output_voltage(VO_5V);
	/* TODO transition delay */
}
