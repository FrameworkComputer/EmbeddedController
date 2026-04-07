/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "common.h"
#include "gpio.h"
#include "power/qcom.h"

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ##args)

/*
 * Set the power good threshold for VPH_PWR(mV).
 *
 * The VPH_PWR "power good" signal was measured and observed to be stable at
 * approximately 2.6V. Consequently, the power good threshold has been
 * configured to 2.0V
 */
#define VPH_PWR_THRESHOLD 2000

/*
 * Set the reset threshold for VPH_PWR(mV).
 *
 * The VPH_PWR during reset should be around 0mV. This threshold is set to
 * 10mV to allow a small margin based on on-board experiments and has no source
 * of truth documentation
 */
#define VPH_PWR_RESET_THRESHOLD 10

void board_set_switchcap_power(int enable)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_vph_pwr_1a), enable);
}

int board_is_switchcap_enabled(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_en_vph_pwr_1a));
}

int board_is_switchcap_power_good(void)
{
	int adc_value = adc_read_channel(ADC_VPH_PWR);
	CPRINTS("switchcap VPH power good ADC value=%d", adc_value);
	return adc_value > VPH_PWR_THRESHOLD;
}

/**
 * Check if the switchcap is in the reset state.
 *
 * @return 1 if the switchcap is in the reset state, 0 otherwise.
 */
int board_is_switchcap_power_reset(void)
{
	int adc_value = adc_read_channel(ADC_VPH_PWR);
	CPRINTS("switchcap VPH reset ADC value=%d", adc_value);
	return adc_value <= VPH_PWR_RESET_THRESHOLD;
}
