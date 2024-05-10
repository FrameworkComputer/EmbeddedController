/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chg_control.h"
#include "gpio.h"
#include "ioexpanders.h"
#include "registers.h"
#include "timer.h"
#include "usb_pd.h"

#define CHG_P5V_POWER 0
#define CHG_VBUS_POWER 1

void chg_reset(void)
{
	/* Disconnect DUT Power */
	chg_power_select(CHG_POWER_OFF);

	/* Disconnect CHG CC1(Rd) and CC2(Rd) */
	chg_attach_cc_rds(0);

	/* Give time for CHG to detach, use tErrorRecovery. */
	crec_msleep(PD_T_ERROR_RECOVERY);

	/* Connect CHG CC1(Rd) and CC2(Rd) to detect charger */
	chg_attach_cc_rds(1);
}

void chg_power_select(enum chg_power_select_t type)
{
	switch (type) {
	case CHG_POWER_OFF:
		dut_chg_en(0);
		vbus_dischrg_en(1);
		break;
	case CHG_POWER_PP5000:
		vbus_dischrg_en(0);
		host_or_chg_ctl(CHG_P5V_POWER);
		dut_chg_en(1);
		break;
	case CHG_POWER_VBUS:
		vbus_dischrg_en(0);
		host_or_chg_ctl(CHG_VBUS_POWER);
		dut_chg_en(1);
		break;
	}
}

void chg_attach_cc_rds(bool en)
{
	if (en) {
		/*
		 * Configure USB_CHG_CC1_MCU and USB_CHG_CC2_MCU as
		 * ANALOG input
		 */
		STM32_GPIO_MODER(GPIO_A) =
			(STM32_GPIO_MODER(GPIO_A) | (3 << (2 * 2)) | /* PA2 in
									ANALOG
									mode */
			 (3 << (2 * 4))); /* PA4 in ANALOG mode */
	} else {
		/*
		 * Configure USB_CHG_CC1_MCU and USB_CHG_CC2_MCU as GPIO and
		 * drive high to trigger disconnect.
		 * NOTE: The CC line has an external fixed Rd pull-down.
		 * Driving the CC line High overrides the pull down and this
		 * triggers a disconnection.
		 */
		/* Set level high */
		gpio_set_level(GPIO_USB_CHG_CC1_MCU, 1);
		gpio_set_level(GPIO_USB_CHG_CC2_MCU, 1);

		/* Disable Analog mode and Enable GPO */
		STM32_GPIO_MODER(GPIO_A) = (STM32_GPIO_MODER(GPIO_A) &
					    ~(3 << (2 * 2) | /* PA2 disable ADC
							      */
					      3 << (2 * 4))) /* PA4 disable ADC
							      */
					   | (1 << (2 * 2) | /* Set as GPO */
					      1 << (2 * 4)); /* Set as GPO */
	}
}
