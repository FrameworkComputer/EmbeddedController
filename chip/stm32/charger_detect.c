/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Detect what adapter is connected */

#include "charge_manager.h"
#include "hooks.h"
#include "registers.h"
#include "timer.h"

static void enable_usb(void)
{
	/* Enable USB device clock. */
	STM32_RCC_APB1ENR |= STM32_RCC_PB1_USB;
}
DECLARE_HOOK(HOOK_INIT, enable_usb, HOOK_PRIO_DEFAULT);

static void disable_usb(void)
{
	/* Disable USB device clock. */
	STM32_RCC_APB1ENR &= ~STM32_RCC_PB1_USB;
}
DECLARE_HOOK(HOOK_SYSJUMP, disable_usb, HOOK_PRIO_DEFAULT);

static uint16_t detect_type(uint16_t det_type)
{
	STM32_USB_BCDR &= 0;
	crec_usleep(1);
	STM32_USB_BCDR |= (STM32_USB_BCDR_BCDEN | det_type);
	crec_usleep(1);
	STM32_USB_BCDR &= ~(STM32_USB_BCDR_BCDEN | det_type);
	return STM32_USB_BCDR;
}

int charger_detect_get_device_type(void)
{
	uint16_t pdet_result;

	if (!(detect_type(STM32_USB_BCDR_DCDEN) & STM32_USB_BCDR_DCDET))
		return CHARGE_SUPPLIER_PD;

	pdet_result = detect_type(STM32_USB_BCDR_PDEN);
	/* TODO: add support for detecting proprietary chargers. */
	if (pdet_result & STM32_USB_BCDR_PDET) {
		if (detect_type(STM32_USB_BCDR_SDEN) & STM32_USB_BCDR_SDET)
			return CHARGE_SUPPLIER_BC12_DCP;
		else
			return CHARGE_SUPPLIER_BC12_CDP;
	} else if (pdet_result & STM32_USB_BCDR_PS2DET)
		return CHARGE_SUPPLIER_PROPRIETARY;
	else
		return CHARGE_SUPPLIER_BC12_SDP;
}
