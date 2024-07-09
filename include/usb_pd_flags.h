/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Contains USB PD flags definition and accessors
 */
#ifndef __CROS_EC_USB_PD_FLAGS_H
#define __CROS_EC_USB_PD_FLAGS_H

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * USB PD VBUS detect (0-2)
 */
enum usb_pd_vbus_detect {
	USB_PD_VBUS_DETECT_UNKNOWN = 0,
	USB_PD_VBUS_DETECT_NONE = 1,
	USB_PD_VBUS_DETECT_TCPC = 2,
	USB_PD_VBUS_DETECT_GPIO = 3,
	USB_PD_VBUS_DETECT_PPC = 4,
	USB_PD_VBUS_DETECT_CHARGER = 5
};

/*
 * USB PD DISCHARGE (Bits 3-4)
 */
enum usb_pd_discharge {
	USB_PD_DISCHARGE_NONE = 0,
	USB_PD_DISCHARGE_TCPC = 1,
	USB_PD_DISCHARGE_GPIO = 2,
	USB_PD_DISCHARGE_PPC = 3
};

/*
 * USB PD Charger OTG (Bit 5)
 */
enum usb_pd_charger_otg {
	USB_PD_CHARGER_OTG_DISABLED = 0,
	USB_PD_CHARGER_OTG_ENABLED = 1
};

union usb_pd_runtime_flags {
	struct {
		enum usb_pd_vbus_detect vbus_detect : 3;
		enum usb_pd_discharge discharge : 2;
		enum usb_pd_charger_otg charger_otg : 1;
		uint32_t reserved : 26;
	};
	uint32_t raw_value;
};

/**
 * Set VBUS detect type from USB_PD_FLAGS.
 */
void set_usb_pd_vbus_detect(enum usb_pd_vbus_detect vbus_detect);

/**
 * Get VBUS detect type from USB_PD_FLAGS.
 *
 * @return the VBUS detect type.
 */
enum usb_pd_vbus_detect get_usb_pd_vbus_detect(void);

/**
 * Set USB PD discharge type from USB_PD_FLAGS.
 */
void set_usb_pd_discharge(enum usb_pd_discharge discharge);

/**
 * Get USB PD discharge type from USB_PD_FLAGS.
 *
 * @return the USB PD discharge type.
 */
enum usb_pd_discharge get_usb_pd_discharge(void);

/**
 * Set USB PD charger OTG from USB_PD_FLAGS.
 */
void set_usb_pd_charger_otg(enum usb_pd_charger_otg charger_otg);

/**
 * Get USB PD charger OTG from USB_PD_FLAGS.
 *
 * @return the USB PD charger OTG.
 */
enum usb_pd_charger_otg get_usb_pd_charger_otg(void);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_PD_FLAGS_H */
