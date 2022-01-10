/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Contains USB PD accessors definition
 */

#include "common.h"
#include "config.h"
#include "usb_pd_flags.h"

static union usb_pd_runtime_flags usb_pd_flags;
BUILD_ASSERT(sizeof(usb_pd_flags) == sizeof(uint32_t));

enum usb_pd_vbus_detect get_usb_pd_vbus_detect(void)
{
	if (IS_ENABLED(CONFIG_USB_PD_RUNTIME_FLAGS))
		return (enum usb_pd_vbus_detect) usb_pd_flags.vbus_detect;
	else if (IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_TCPC))
		return (enum usb_pd_vbus_detect)USB_PD_VBUS_DETECT_TCPC;
	else if (IS_ENABLED(CONFIG_USD_PD_VBUS_DETECT_GPIO))
		return (enum usb_pd_vbus_detect)USB_PD_VBUS_DETECT_GPIO;
	else if (IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_PPC))
		return (enum usb_pd_vbus_detect)USB_PD_VBUS_DETECT_PPC;
	else if (IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_CHARGER))
		return (enum usb_pd_vbus_detect)USB_PD_VBUS_DETECT_CHARGER;
	else if (IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_NONE))
		return (enum usb_pd_vbus_detect)USB_PD_VBUS_DETECT_NONE;
	else
		return (enum usb_pd_vbus_detect)USB_PD_VBUS_DETECT_UNKNOWN;
}

enum usb_pd_discharge get_usb_pd_discharge(void)
{
	if (IS_ENABLED(CONFIG_USB_PD_RUNTIME_FLAGS))
		return (enum usb_pd_discharge)usb_pd_flags.discharge;
	else if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE_TCPC))
		return (enum usb_pd_discharge)USB_PD_DISCHARGE_TCPC;
	else if (IS_ENABLED(CONFIG_USD_PD_DISCHARGE_GPIO))
		return (enum usb_pd_discharge)USB_PD_DISCHARGE_GPIO;
	else if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE_PPC))
		return (enum usb_pd_discharge)USB_PD_DISCHARGE_PPC;
	else
		return (enum usb_pd_discharge)USB_PD_DISCHARGE_NONE;
}

enum usb_pd_charger_otg get_usb_pd_charger_otg(void)
{
	return usb_pd_flags.charger_otg;
}

void set_usb_pd_vbus_detect(enum usb_pd_vbus_detect vbus_detect)
{
	usb_pd_flags.vbus_detect = vbus_detect;
}

void set_usb_pd_discharge(enum usb_pd_discharge discharge)
{
	usb_pd_flags.discharge = discharge;
}

void set_usb_pd_charger_otg(enum usb_pd_charger_otg charger_otg)
{
	usb_pd_flags.charger_otg = charger_otg;
}
