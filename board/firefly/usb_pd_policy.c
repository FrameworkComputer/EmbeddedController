/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "board.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/* Acceptable margin between requested VBUS and measured value */
#define MARGIN_MV 400 /* mV */

/* we are not acting as a source */
const uint32_t pd_src_pdo[] = {
		PDO_FIXED(5000,   500, PDO_FIXED_EXTERNAL),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);

/* Fake PDOs : we just want our pre-defined voltages */
const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000,   500, 0),
		PDO_FIXED(12000,  500, 0),
		PDO_FIXED(20000,  500, 0),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

/* Desired voltage requested as a sink (in millivolts) */
static unsigned select_mv = 20000;

int pd_choose_voltage(int cnt, uint32_t *src_caps, uint32_t *rdo,
		      uint32_t *curr_limit, uint32_t *supply_voltage)
{
	int i;
	int ma;
	int set_mv = select_mv;

	/* Default to 5V */
	if (set_mv <= 0)
		set_mv = 5000;

	/* Get the selected voltage */
	for (i = cnt; i >= 0; i--) {
		int mv = ((src_caps[i] >> 10) & 0x3FF) * 50;
		int type = src_caps[i] & PDO_TYPE_MASK;
		if ((mv == set_mv) && (type == PDO_TYPE_FIXED))
			break;
	}
	if (i < 0)
		return -EC_ERROR_UNKNOWN;

	/* request all the power ... */
	ma = 10 * (src_caps[i] & 0x3FF);
	*rdo = RDO_FIXED(i + 1, ma, ma, 0);
	CPRINTF("Request [%d] %dV %dmA\n", i, set_mv/1000, ma);
	*curr_limit = ma;
	*supply_voltage = set_mv;
	return EC_SUCCESS;
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	/* No battery, nothing to do */
	return;
}

void pd_set_max_voltage(unsigned mv)
{
	select_mv = mv;
}

int pd_request_voltage(uint32_t rdo)
{
	/* Never acting as a source */
	return EC_ERROR_INVAL;
}

int pd_set_power_supply_ready(int port)
{
	/* Never acting as a source */
	return EC_ERROR_INVAL;
}

void pd_power_supply_reset(int port)
{
}

int pd_board_checks(void)
{
	static int blinking;
	int vbus;
	int led5 = 0, led12 = 0, led20 = 0;

	/* LED blinking state for the default indicator */
	blinking = (blinking + 1) & 3;

	vbus = adc_read_channel(ADC_CH_VBUS_SENSE);

	if (select_mv > 0) {
		/* is current VBUS voltage matching the request ? */
		int diff = vbus - select_mv;
		int correct = (diff < MARGIN_MV) && (diff > -MARGIN_MV);
		/*
		 * turn on the LED if the voltage is correct
		 * or we are in on-period of the duty cycle.
		 */
		int led_value = correct || !blinking;
		/* decide which LED is used */
		switch (select_mv) {
		case 5000:
			led5 = led_value;
			break;
		case 12000:
			led12 = led_value;
			break;
		case 20000:
			led20 = led_value;
			break;
		}
	}
	/* switch  LEDs */
	gpio_set_level(GPIO_LED_PP5000, led5);
	gpio_set_level(GPIO_LED_PP12000, led12);
	gpio_set_level(GPIO_LED_PP20000, led20);

	return EC_SUCCESS;
}

int pd_power_swap(int port)
{
	/* Always refuse power swap */
	return 0;
}
