/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Stellaris EKB-LM4F-EAC board-specific configuration */

#include "board.h"
#include "chipset.h"
#include "gpio.h"
#include "power_button.h"
#include "registers.h"
#include "util.h"
#include "lm4_adc.h"
#include "adc.h"

#ifndef CONFIG_TASK_POWERBTN
#define power_button_interrupt NULL
#endif

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[ADC_CH_COUNT] =
{
	/* EC internal temperature is calculated by
	 * 273 + (295 - 450 * ADC_VALUE / ADC_READ_MAX) / 2
	 * = -225 * ADC_VALUE / ADC_READ_MAX + 420.5
	 */
	{"ECTemp", LM4_ADC_SEQ0, -225, ADC_READ_MAX, 420,
	 LM4_AIN_NONE, 0x0e /* TS0 | IE0 | END0 */},

	/* Charger current is mapped from 0~4000mA to 0~1.6V.
	 * And ADC maps 0~3.3V to ADC_READ_MAX.
	 *
	 * Note that on BDS, this is really just the turn pot on the Badger
	 * board, but that's good enough for debugging the ADC.
	 */
	{"ChargerCurrent", LM4_ADC_SEQ1, 33 * 4000, ADC_READ_MAX * 16, 0,
	 LM4_AIN(0), 0x06 /* IE0 | END0 */},
};


/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTONn", LM4_GPIO_C, (1<<5), GPIO_PULL_UP | GPIO_INT_BOTH,
	 power_button_interrupt},
	{"LID_SWITCHn",   LM4_GPIO_D, (1<<0), GPIO_PULL_UP | GPIO_INT_BOTH,
	 power_button_interrupt},
	/* Other inputs */
	/* Outputs */
	{"DEBUG_LED",    LM4_GPIO_A, (1<<7), GPIO_OUT_LOW, NULL},
	/* Unimplemented signals which we need to emulate for now */
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_WAKEn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_PWRBTNn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_BKLTEN"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SLP_An"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SLP_ME_CSW_DEVn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SLP_S3n"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SLP_S4n"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SLP_S5n"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SLP_SUSn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SUSWARNn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PGOOD_1_5V_DDR"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PGOOD_1_5V_PCH"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PGOOD_1_8VS"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PGOOD_5VALW"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PGOOD_CPU_CORE"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PGOOD_VCCP"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PGOOD_VCCSA"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PGOOD_VGFX_CORE"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENABLE_1_5V_DDR"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENABLE_BACKLIGHT"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENABLE_VCORE"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("ENABLE_VS"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_DPWROK"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_PWROK"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_RSMRSTn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SMIn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("PCH_SUSACKn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("SHUNT_1_5V_DDR"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("RECOVERYn"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("WRITE_PROTECT"),
	GPIO_SIGNAL_NOT_IMPLEMENTED("LIGHTBAR_RESETn"),
};


/* BDS system is only half-wired to an x86 chipset, so it can't tell what state
 * the chipset is in.  Rather than scatter ifdef's everywhere, put a mock
 * chipset interface here. */
int chipset_in_state(int state_mask)
{
	return 1;  /* Sure, I'm in whatever state you want. */
}


void chipset_exit_hard_off(void)
{
}


void configure_board(void)
{
}
