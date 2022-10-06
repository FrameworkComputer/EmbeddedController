/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Goroh board configuration */

#include "adc.h"
#include "button.h"
#include "charge_manager.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/als_tcs3400.h"
#include "driver/charger/isl923x.h"
#include "driver/ppc/syv682x.h"
#include "driver/tcpm/it83xx_pd.h"
#include "driver/temp_sensor/thermistor.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "spi.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"
#include "temp_sensor.h"
#include "timer.h"
#include "uart.h"
#include "usb_mux.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ##args)

/* Initialize board. */
static void board_init(void)
{
	/* Enable motion sensor interrupt */
	gpio_enable_interrupt(GPIO_BASE_IMU_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
	{ "BOARD_ID", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH1 },
	{ "TEMP_CPU", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH2 },
	{ "TEMP_GPU", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH3 },
	{ "TEMP_CHARGER", ADC_MAX_MVOLT, ADC_READ_MAX + 1, 0, CHIP_ADC_CH5 },
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels.  */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_LED_GREEN] = { .channel = PWM_HW_CH_DCR0,
			       .flags = PWM_CONFIG_DSLEEP |
					PWM_CONFIG_OPEN_DRAIN |
					PWM_CONFIG_ACTIVE_LOW,
			       .freq_hz = 324, /* maximum supported frequency */
			       .pcfsr_sel = PWM_PRESCALER_C4 },
	[PWM_CH_LED_RED] = { .channel = PWM_HW_CH_DCR1,
			     .flags = PWM_CONFIG_DSLEEP |
				      PWM_CONFIG_OPEN_DRAIN |
				      PWM_CONFIG_ACTIVE_LOW,
			     .freq_hz = 324, /* maximum supported frequency */
			     .pcfsr_sel = PWM_PRESCALER_C4 },
	[PWM_CH_FAN] = { .channel = PWM_HW_CH_DCR2,
			 .flags = PWM_CONFIG_OPEN_DRAIN,
			 .freq_hz = 25000, /* maximum supported frequency */
			 .pcfsr_sel = PWM_PRESCALER_C4 },
	[PWM_CH_KBLIGHT] = { .channel = PWM_HW_CH_DCR3,
			     .flags = PWM_CONFIG_DSLEEP,
			     .freq_hz = 25000,
			     .pcfsr_sel = PWM_PRESCALER_C4 },
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

static void board_suspend(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_suspend, HOOK_PRIO_DEFAULT);

static void board_resume(void)
{
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_resume, HOOK_PRIO_DEFAULT);
