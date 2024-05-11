/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Thunderbolt-compatible mode header.
 */

#ifndef __CROS_EC_USB_PD_TBT_COMPAT_H
#define __CROS_EC_USB_PD_TBT_COMPAT_H

#include "usb_pd_vdo.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NOTE: Throughout the file, some of the bit fields in the structures are for
 * information purpose, they might not be actually used in the current code.
 * When appropriate, replace the bit fields in the structures with appropriate
 * enums.
 */

/*
 * Reference: USB Type-C cable and connector specification, Release 2.0
 */

/*****************************************************************************/
/*
 * TBT3 Device Discover Identity Responses
 */

/*
 * Table F-8 TBT3 Device Discover Identity VDO Responses
 * -------------------------------------------------------------
 * <31>    : USB Communications Capable as USB Host
 *           0b = No
 *           1b = Yes
 * <30>    : USB Communications Capable as a USB Device
 *           0b = No
 *           1b = Yes
 * <29:27> : Product Type (UFP)
 *           001b = PDUSB Hub
 *           010b = PDUSB Peripheral
 *           101b = Alternate Mode Adapter (AMA)
 *           110b = VCONN-Powered USB Device (VPD)
 * <26>    : Modal Operation Supported Modal Operation Supported
 *           0b = No
 *           1b = Yes
 * <25:23> : Product Type (DFP)
 *           001b = PDUSB Hub
 *           010b = PDUSB Host
 *           100b = Alternate Mode Controller (AMC)
 * <22:16> : 0 Reserved
 * <15:0>  : Per vendor USB Vendor ID
 */

/*****************************************************************************/
/*
 * TBT3 Discover SVID Responses
 */

/*
 * Table F-9 TBT3 Discover SVID VDO Responses
 * -------------------------------------------------------------
 * Note: These SVID can be in any order
 * <31:16> : 0x8087 = Intel/TBT3 SVID 0
 * <B15:0> : 0xFF01 = VESA DP (if supported) SVID 1
 */

/*****************************************************************************/
/*
 * TBT3 Device Discover Mode Responses
 */

/*
 * Table F-10 TBT3 Device Discover Mode VDO Responses
 * -------------------------------------------------------------
 * <31>    : Vendor specific B1
 *           0b = Not supported
 *           1b = Supported
 * <30>    : Vendor specific B0
 *           0b = Not supported
 *           1b = Supported
 * <29:27> : Reserved
 * <26>    : Intel specific B0
 *           0b = Not supported
 *           1b = Supported
 * <25:17> : Reserved
 * <16>    : TBT Adapter
 *           Errata: TBT Adapter bits are swapped in the document
 *           Refer USB Type-C ENGINEERING CHANGE NOTICE (ECN)
 *           "USB Type-C ECN Thunderbolt 3 Compatibility Updates.pdf"
 *           with Title: Thunderbolt 3 Compatibility Updates
 *           for the document fix published by USB-IF.
 *           0b = TBT3 Adapter
 *           1b = TBT2 Legacy Adapter
 * <15:0>  : TBT Alternate Mode
 *           0x0001 = TBT Mode
 */
enum tbt_adapter_type {
	TBT_ADAPTER_TBT3,
	TBT_ADAPTER_TBT2_LEGACY,
};

enum vendor_specific_support {
	VENDOR_SPECIFIC_NOT_SUPPORTED,
	VENDOR_SPECIFIC_SUPPORTED,
};

/* TBT Alternate Mode */
#define TBT_ALTERNATE_MODE 0x0001
#define PD_VDO_RESP_MODE_INTEL_TBT(x) (((x) & 0xff) == TBT_ALTERNATE_MODE)

union tbt_mode_resp_device {
	struct {
		uint16_t tbt_alt_mode : 16;
		enum tbt_adapter_type tbt_adapter : 1;
		uint16_t reserved0 : 9;
		enum vendor_specific_support intel_spec_b0 : 1;
		uint8_t reserved1 : 3;
		enum vendor_specific_support vendor_spec_b0 : 1;
		enum vendor_specific_support vendor_spec_b1 : 1;
	};
	uint32_t raw_value;
};

/*
 * Table F-11 TBT3 Cable Discover Mode VDO Responses
 * -------------------------------------------------------------
 * <31:26> : Reserved
 * <25>    : Active Passive
 *           Errata: Reserved B25 has been changed to Active passive bit
 *           Refer USB Type-C ENGINEERING CHANGE NOTICE (ECN)
 *           "USB Type-C ECN Thunderbolt 3 Compatibility Updates.pdf"
 *           with Title: Thunderbolt 3 Compatibility Updates
 *           for the document fix published by USB-IF.
 *           0b = Passive cable
 *           1b = Active cable
 *           NOTE: This change is only applicable to rev 3 cables
 * <24>    : Reserved
 * <23>    : Active Cable Plug Link Training
 *           0 = Active with bi-directional LSRX1 communication or when Passive
 *           1 = Active with uni-directional LSRX1 communication
 * <22>    : Re-timer
 *           0 = Not re-timer
 *           1 = Re-timer
 * <21>    : Cable Type
 *           0b = Non-Optical
 *           1b = Optical
 * <20:19> : TBT_Rounded_Support
 *           00b = 3rd Gen Non-Rounded TBT
 *           01b = 3rd & 4th Gen Rounded and Non-Rounded TBT
 *           10b..11b = Reserved
 * <18:16> : Cable Speed
 *           000b = Reserved
 *           001b = USB3.1 Gen1 Cable (10 Gbps TBT support)
 *           010b = 10 Gbps (USB 3.2 Gen1 and Gen2 passive cables)
 *           011b = 10 Gbps and 20 Gbps (TBT 3rd Gen active cables and
 *                  20 Gbps passive cables)
 *           100b..111b = Reserved
 * <15:0>  : TBT Alternate Mode
 *           0x0001 = TBT Mode
 */
enum tbt_active_passive_cable {
	TBT_CABLE_PASSIVE,
	TBT_CABLE_ACTIVE,
};

enum tbt_compat_cable_speed {
	TBT_SS_RES_0,
	TBT_SS_U31_GEN1,
	TBT_SS_U32_GEN1_GEN2,
	TBT_SS_TBT_GEN3,
	TBT_SS_RES_4,
	TBT_SS_RES_5,
	TBT_SS_RES_6,
	TBT_SS_RES_7,
};

enum tbt_cable_type {
	TBT_CABLE_NON_OPTICAL,
	TBT_CABLE_OPTICAL,
};

enum tbt_compat_rounded_support {
	TBT_GEN3_NON_ROUNDED,
	TBT_GEN3_GEN4_ROUNDED_NON_ROUNDED,
	TBT_ROUND_SUP_RES_2,
	TBT_ROUND_SUP_RES_3,
};

enum usb_retimer_type {
	USB_NOT_RETIMER,
	USB_RETIMER,
};

enum link_lsrx_comm {
	BIDIR_LSRX_COMM,
	UNIDIR_LSRX_COMM,
};

union tbt_mode_resp_cable {
	struct {
		uint16_t tbt_alt_mode : 16;
		enum tbt_compat_cable_speed tbt_cable_speed : 3;
		enum tbt_compat_rounded_support tbt_rounded : 2;
		enum tbt_cable_type tbt_cable : 1;
		enum usb_retimer_type retimer_type : 1;
		enum link_lsrx_comm lsrx_comm : 1;
		uint8_t reserved1 : 1;
		enum tbt_active_passive_cable tbt_active_passive : 1;
		uint8_t reserved0 : 6;
	};
	uint32_t raw_value;
};

/*****************************************************************************/
/*
 * TBT3 Enter Mode Command
 */

/*
 * Table F-13 TBT3 Device Enter Mode Command SOP
 * -------------------------------------------------------------
 * <31>    : Vendor specific B1
 *           0b = Not supported
 *           1b = Supported
 * <30>    : Vendor specific B0
 *           0b = Not supported
 *           1b = Supported
 * <29:27> : 000b Reserved
 * <26>    : Intel specific B0
 *           0b = Not supported
 *           1b = Supported
 * <25>    : Active_Passive
 *           Errata: Active_Passive bit is changed to B25
 *           Refer USB Type-C ENGINEERING CHANGE NOTICE (ECN)
 *           "USB Type-C ECN Thunderbolt 3 Compatibility Updates.pdf"
 *           with Title: Thunderbolt 3 Compatibility Updates
 *           for the document fix published by USB-IF.
 *           0b = Passive cable
 *           1b = Active cable
 * <24>    : TBT adapter
 *           Errata: B24 represents Thunderbolt Adapter type
 *           Refer USB Type-C ENGINEERING CHANGE NOTICE (ECN)
 *           "USB Type-C ECN Thunderbolt 3 Compatibility Updates.pdf"
 *           with Title: Thunderbolt 3 Compatibility Updates
 *           for the document fix published by USB-IF.
 *           0b = TBT3 Adapter
 *           1b = TBT2 Legacy Adapter
 * <23>    : Active Cable Link Training
 *           0b = Active with bi-directional LSRX1 communication or when Passive
 *           1b = Active with uni-directional LSRX1 communication
 * <22>    : Re-timer
 *           0b = Not re-timer
 *           1b = Re-timer
 * <21>    : Cable Type
 *           0b = Non-Optical
 *           1b = Optical
 * <20:19> : TBT_Rounded_Support
 *           00b = 3rd Gen Non-Rounded TBT
 *           01b = 3rd & 4th Gen Rounded and Non-Rounded TBT
 *           10b..11b = Reserved
 * <18:16> : Cable Speed
 *           000b = Reserved
 *           001b = USB3.1 Gen1 Cable (10 Gbps TBT support)
 *           010b = 10 Gbps (USB 3.2 Gen1 and Gen2 passive cables)
 *           011b = 10 Gbps and 20 Gbps (TBT 3rd Gen active cables and
 *                  20 Gbps passive cables)
 *           100b..111b = Reserved
 * <15:0>  : TBT Alternate Mode
 *           0x0001 = TBT Mode
 */
enum tbt_enter_cable_type {
	TBT_ENTER_PASSIVE_CABLE,
	TBT_ENTER_ACTIVE_CABLE,
};

union tbt_dev_mode_enter_cmd {
	struct {
		uint16_t tbt_alt_mode : 16;
		enum tbt_compat_cable_speed tbt_cable_speed : 3;
		enum tbt_compat_rounded_support tbt_rounded : 2;
		enum tbt_cable_type tbt_cable : 1;
		enum usb_retimer_type retimer_type : 1;
		enum link_lsrx_comm lsrx_comm : 1;
		enum tbt_adapter_type tbt_adapter : 1;
		enum tbt_enter_cable_type cable : 1;
		enum vendor_specific_support intel_spec_b0 : 1;
		uint8_t reserved1 : 3;
		enum vendor_specific_support vendor_spec_b0 : 1;
		enum vendor_specific_support vendor_spec_b1 : 1;
	};
	uint32_t raw_value;
};

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_PD_TBT_COMPAT_H */
