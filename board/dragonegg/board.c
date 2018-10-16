/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* DragonEgg board-specific configuration */
#include "adc.h"
#include "adc_chip.h"
#include "button.h"
#include "common.h"
#include "charger.h"
#include "console.h"
#include "driver/ppc/nx20p348x.h"
#include "driver/ppc/sn5s330.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "intc.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "system.h"
#include "uart.h"
#include "usb_pd.h"
#include "util.h"

static void ppc_interrupt(enum gpio_signal signal)
{

	switch (signal) {
	case GPIO_USB_C0_TCPPC_INT_L:
		sn5s330_interrupt(0);
		break;

	case GPIO_USB_C2_TCPPC_INT_ODL:
		nx20p348x_interrupt(2);
		break;

	default:
		break;
	}
}

static void tcpc_alert_event(enum gpio_signal signal)
{
	int port = -1;

	/*
	 * Since C0/C1 TCPC are embedded within EC, we don't need the PDCMD
	 * tasks.The (embedded) TCPC status since chip driver code will
	 * handles its own interrupts and forward the correct events to
	 * the PD_C0/1 task. See it83xx/intc.c
	 */
	switch (signal) {
	case GPIO_USB_C2_TCPC_INT_ODL:
		port = 2;
		break;
	default:
		return;
	}

	schedule_deferred_pd_interrupt(port);
}

#include "gpio_list.h" /* Must come after other header files. */

/******************************************************************************/
/* ADC channels */
const struct adc_t adc_channels[] = {
	/* Vbus C0 sensing (7.3x voltage divider). PPVAR_USB_C0_VBUS */
	[ADC_VBUS_C0] = {.name = "VBUS_C0",
			 .factor_mul = (ADC_MAX_MVOLT * 73) / 10,
			 .factor_div = ADC_READ_MAX + 1,
			 .shift = 0,
			 .channel = CHIP_ADC_CH1},
	/* Vbus C1 sensing (7.3x voltage divider). PPVAR_USB_C1_VBUS */
	[ADC_VBUS_C1] = {.name = "VBUS_C1",
			 .factor_mul = (ADC_MAX_MVOLT * 73) / 10,
			 .factor_div = ADC_READ_MAX + 1,
			 .shift = 0,
			 .channel = CHIP_ADC_CH0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* SPI devices */
/* TODO(b/110880394): Fill out correctly (SPI FLASH) */
const struct spi_device_t spi_devices[] = {
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

/******************************************************************************/
/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT]   = { .channel = 0, .flags = 0, .freq_hz = 100 },
	[PWM_CH_LED_RED]   = { .channel = 2, .flags = PWM_CONFIG_DSLEEP |
			       PWM_CONFIG_ACTIVE_LOW, .freq_hz = 100 },
	[PWM_CH_LED_GREEN] = { .channel = 1, .flags = PWM_CONFIG_DSLEEP |
			       PWM_CONFIG_ACTIVE_LOW, .freq_hz = 100 },
	[PWM_CH_LED_BLUE]  = { .channel = 3, .flags = PWM_CONFIG_DSLEEP |
			       PWM_CONFIG_ACTIVE_LOW, .freq_hz = 100 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* GPIO to enable/disable the USB Type-A port. */
const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_EN_USB_A_5V,
};

void board_overcurrent_event(int port)
{
	if (port == 0) {
		/* TODO(b/111281797): When does this get set high again? */
		gpio_set_level(GPIO_USB_OC_ODL, 0);
		cprints(CC_USBPD, "p%d: overcurrent!", port);
	}
}

static void board_disable_learn_mode(void)
{
	/* Disable learn mode after checking to make sure AC is still present */
	if (extpower_is_present())
		charger_discharge_on_ac(0);
}
DECLARE_DEFERRED(board_disable_learn_mode);

static void board_extpower(void)
{
	/*
	 * For the bq25710 charger, we need the switching converter to remain
	 * disabled until ~130 msec from when VBUS present to allow the
	 * converter to be biased properly. Otherwise, there will be a reverse
	 * buck/boost until the converter is biased. The recommendation is to
	 * exit learn mode 200 msec after external charger is connected.
	 *
	 * TODO(b/112372451): When there are updated versions of the bq25710,
	 * this set of changes can be removed.
	 */
	if (extpower_is_present()) {
		hook_call_deferred(&board_disable_learn_mode_data, 200 * MSEC);
	} else {
		/* Enable charger learn mode */
		charger_discharge_on_ac(1);
		/* Cancel any pending call to disable learn mode */
		hook_call_deferred(&board_disable_learn_mode_data, -1);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, board_extpower, HOOK_PRIO_DEFAULT);

/* Initialize board. */
static void board_init(void)
{
	/*
	 * On EC reboot, need to always set battery learn mode to the correct
	 * state based on presence of AC.
	 */
	board_extpower();
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);
