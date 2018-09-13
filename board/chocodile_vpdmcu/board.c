/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* chocodile board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
#include "task.h"
#include "usb_pd.h"
#include "usb_pd_tcpc.h"
#include "util.h"
#include "vpd_api.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)

void board_config_pre_init(void)
{
	/* enable SYSCFG clock */
	STM32_RCC_APB2ENR |= 1 << 0;
}

#include "gpio_list.h"

/* Initialize board. */
static void board_init(void)
{
	/* Do nothing */
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/* ADC channels */
const struct adc_t adc_channels[] = {
	/* USB PD CC lines sensing. Converted to mV (3300mV/4096). */
	[ADC_VCONN_VSENSE] =  {
	"VCONN_VSENSE",  3000, 4096, 0, STM32_AIN(ADC_VCONN_VSENSE)},
	[ADC_CC_VPDMCU] =     {
	"CC_VPDMCU",     3000, 4096, 0, STM32_AIN(ADC_CC_VPDMCU)},
	[ADC_CC_RP3A0_RD_L] = {
	"CC_RP3A0_RD_L", 3000, 4096, 0, STM32_AIN(ADC_CC_RP3A0_RD_L)},
	[ADC_RDCONNECT_REF] = {
	"RDCONNECT_REF", 3000, 4096, 0, STM32_AIN(ADC_RDCONNECT_REF)},
	[ADC_CC1_RP3A0_RD_L] = {
	"CC1_RP1A5_ODH", 3000, 4096, 0, STM32_AIN(ADC_CC1_RP3A0_RD_L)},
	[ADC_CC2_RP3A0_RD_L] = {
	"CC2_RP1A5_ODH", 3000, 4096, 0, STM32_AIN(ADC_CC2_RP3A0_RD_L)},
	[ADC_HOST_VBUS_VSENSE] = {
	"HOST_VBUS_VSENSE", 3000, 4096, 0, STM32_AIN(ADC_HOST_VBUS_VSENSE)},
	[ADC_CHARGE_VBUS_VSENSE] = {
	"CHARGE_VBUS_VSENSE", 3000, 4096, 0, STM32_AIN(ADC_CHARGE_VBUS_VSENSE)},
	[ADC_CC1_RPUSB_ODH] = {
	"CC1_RPUSB_ODH", 3000, 4096, 0, STM32_AIN(ADC_CC1_RPUSB_ODH)},
	[ADC_CC2_RPUSB_ODH] = {
	"CC2_RPUSB_ODH", 3000, 4096, 0, STM32_AIN(ADC_CC2_RPUSB_ODH)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

void tcpc_alert_clear(int port)
{
	/* Do nothing */
}
