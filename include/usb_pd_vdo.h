/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-PD Cable type header.
 */

#ifndef __CROS_EC_USB_PD_VDO_H
#define __CROS_EC_USB_PD_VDO_H

#include <stdint.h>

/*
 * NOTE: Throughout the file, some of the bit fields in the structures are for
 * information purpose, they might not be actually used in the current code.
 * When appropriate, replace the bit fields in the structures with appropriate
 * enums.
 */

/*
 * ############################################################################
 *
 * Reference: USB Power Delivery Specification Revision 3.0, Version 2.0
 * Updated to ECN released on Feb 07, 2020
 *
 * ############################################################################
 */

/*****************************************************************************/
/*
 * Table 6-29 ID Header VDO
 * -------------------------------------------------------------
 * <31>    : USB Communications Capable as USB Host
 * <30>    : USB Communications Capable as a USB Device
 * <29:27> : Product Type (UFP):
 *           000b = Undefined
 *           001b = PDUSB Hub
 *           010b = PDUSB Peripheral
 *           011b = PSD (PD 3.0)
 *           101b = Alternate Mode Adapter (AMA)
 *           110b = Vconn-Powered USB Device (VPD, PD 3.0)
 *           111b = Reserved, shall NOT be used
 *
 *           Product Type (Cable Plug):
 *           000b = Undefined
 *           001b...010b = Reserved, Shall NOT be used
 *           011b = Passive Cable
 *           100b = Active Cable
 *           101b...111b = Reserved, Shall NOT be used
 * <26>    : Modal Operation Supported
 * <25:23> : Product Type (DFP):
 *           000b = Undefined
 *           001b = PDUSB Hub
 *           010b = PDUSB Host
 *           011b = Power Brick
 *           100b = Alternate Mode Controller (AMC)
 *           101b...111b = Reserved, Shall NOT be used
 * <22:21> : Connector Type
 *           00b = Reserved for compatibility with legacy systems
 *           01b = Reserved, Shall Not be used
 *           10b = USB Type-C Receptacle
 *           11b = USB Type-C Captive Plug
 * <20:16> : Reserved
 * <15:0>  : USB Vendor ID
 */
enum connector_type {
	USB_TYPEC_RECEPTACLE = 2,
	USB_TYPEC_CAPTIVE_PLUG,
};

enum idh_ptype_dfp {
	IDH_PTYPE_DFP_UNDEFINED,
	IDH_PTYPE_DFP_HUB,
	IDH_PTYPE_DFP_HOST,
	IDH_PTYPE_DFP_POWER_BRICK,
	IDH_PTYPE_DFP_AMC,
};
/*****************************************************************************/
/*
 * Table 6-33 Cert Stat VDO (Note: same as Revision 2.0)
 * -------------------------------------------------------------
 * <31:0>  : XID assigned by USB-IF
 */
struct cert_stat_vdo {
	uint32_t xid;
};

/*****************************************************************************/
/*
 * Table 6-34 Product VDO (Note: same as Revision 2.0)
 * -------------------------------------------------------------
 * <31:16> : USB Product ID
 * <15:0>  : bcdDevice
 */
struct product_vdo {
	uint16_t bcd_device;
	uint16_t product_id;
};

/*****************************************************************************/
/*
 * USB PD r 3.1 v 1.8 Table 6-39 UFP VDO
 * -------------------------------------------------------------
 * <31:29> : UFP VDO version
 *           Version 1.0 = 000b
 *           Version 1.1 = 001b
 *           Version 1.3 = 011b
 *           Values 100b...111b are Reserved and Shall Not be used
 * <28>    : Reserved
 * <27:24> : Device Capability
 *           0001b = USB2.0 Device capable
 *           0010b = USB2.0 Device capable (Billboard only)
 *           0100b = USB3.2 Device capable
 *           1000b = USB4 Device Capable
 * <23:22> : Connector Type
 *           As of PD r 3.1 v 1.8, this field is legacy and Shall be set to 00b
 *           Values as of PD r 3.0 v 2.0
 *           00b = Reserved, Shall Not be used
 *           01b = Reserved, Shall Not be used
 *           10b = USB Type-C Receptacle
 *           11b = USB Type-C Captive Plug
 * <21:11> : Reserved
 * <10:8>  : VCONN Power
 *           000b = 1W
 *           001b = 1.5W
 *           010b = 2W
 *           011b = 3W
 *           100b = 4W
 *           101b = 5W
 *           110b = 6W
 *           111b = Reserved, Shall Not be used
 *           When VCONN Required field is set to No, the VCONN Power Field is
 *           Reserved and Shall be set to zero.
 * <7>     : VCONN Required
 *           0 = No
 *           1 = Yes
 * <6>     : VBUS Required
 *           0 = No
 *           1 = Yes
 * <5:3>   : Alternate Modes
 *           001b = Supports TBT3 alternate mode
 *           010b = Supports Alternate Modes that reconfigure
 *                  the signals on the [USB Type-C 2.0] connector
 *                  – except for [TBT3]
 *           100b = Supports Alternate Modes that do not
 *                  reconfigure the signals on the [USB Type-C 2.0]
 *                  connector
 * <2:0>   : USB Highest Speed
 *           000b = USB 2.0 only, no SuperSpeed support
 *           001b = USB 3.2 Gen1
 *           010b = USB 3.2/USB4 Gen2
 *           011b = USB4 Gen3
 *           100b…111b = Reserved, Shall Not be used
 */

enum usb_rev30_ss {
	USB_R30_SS_U2_ONLY,
	USB_R30_SS_U32_U40_GEN1,
	USB_R30_SS_U32_U40_GEN2,
	USB_R30_SS_U40_GEN3,
	USB_R30_SS_RES_4,
	USB_R30_SS_RES_5,
	USB_R30_SS_RES_6,
	USB_R30_SS_RES_7,
};

enum usb_pd_vconn_power {
	USB_PD_VCONN_POWER_1W = 0,
	USB_PD_VCONN_POWER_1_5W,
	USB_PD_VCONN_POWER_2W,
	USB_PD_VCONN_POWER_3W,
	USB_PD_VCONN_POWER_4W,
	USB_PD_VCONN_POWER_5W,
	USB_PD_VCONN_POWER_6W,
};

union ufp_vdo_rev30 {
	struct {
		enum usb_rev30_ss usb_highest_speed : 3;
		unsigned int alternate_modes : 3;
		unsigned int vbus_required : 1;
		unsigned int vconn_required : 1;
		enum usb_pd_vconn_power vconn_power : 3;
		unsigned int reserved1 : 11;
		unsigned int connector_type : 2;
		unsigned int device_capability : 4;
		unsigned int reserved2 : 1;
		unsigned int ufp_vdo_version : 3;
	};
	uint32_t raw_value;
};

#define PD_PRODUCT_IS_USB4(vdo) ((vdo) >> 24 & BIT(3))
#define PD_PRODUCT_IS_TBT3(vdo) ((vdo) >> 3 & BIT(0))

/* UFP VDO Version 1.2; update the value when UFP VDO version changes */
#define VDO_UFP1(cap, ctype, alt, speed)                             \
	((0x2) << 29 | ((cap) & 0xf) << 24 | ((ctype) & 0x3) << 22 | \
	 ((alt) & 0x7) << 3 | ((speed) & 0x7))

/* UFP VDO 1 Alternate Modes */
#define VDO_UFP1_ALT_MODE_TBT3 BIT(0)
#define VDO_UFP1_ALT_MODE_RECONFIGURE BIT(1)
#define VDO_UFP1_ALT_MODE_NO_RECONFIGURE BIT(2)
#define VDO_UFP1_ALT_MODE_MASK (0x7 << 3)

/* UFP VDO 1 Device Capability */
#define VDO_UFP1_CAPABILITY_USB20 BIT(0)
#define VDO_UFP1_CAPABILITY_USB20_BILLBOARD BIT(1)
#define VDO_UFP1_CAPABILITY_USB32 BIT(2)
#define VDO_UFP1_CAPABILITY_USB4 BIT(3)

/*****************************************************************************/
/*
 * Table 6-37 DFP VDO
 * -------------------------------------------------------------
 * <31:29> : DFP VDO version
 *           Version 1.0 = 000b
 *           Version 1.1 = 001b
 *           Values 010b...111b are Reserved and Shall Not be used
 * <28:27> : Reserved
 * <26:24> : Host Capability
 *           001b = USB2.0 host capable
 *           010b = USB3.2 host capable
 *           100b = USB4 host capable
 * <23:22> : Connector Type
 *           00b = Reserved, Shall Not be used
 *           01b = Reserved, Shall Not be used
 *           10b = USB Type-C Receptacle
 *           11b = USB Type-C Captive Plug
 * <21:5>  : Reserved
 * <4:0>   : Port number
 */
/* DFP VDO Version 1.1; update the value when DFP VDO version changes */
#define VDO_DFP(cap, ctype, port)                                    \
	((0x1) << 29 | ((cap) & 0x7) << 24 | ((ctype) & 0x3) << 22 | \
	 ((port) & 0x1f))

/* DFP VDO Host Capability */
#define VDO_DFP_HOST_CAPABILITY_USB20 BIT(0)
#define VDO_DFP_HOST_CAPABILITY_USB32 BIT(1)
#define VDO_DFP_HOST_CAPABILITY_USB4 BIT(2)

/*****************************************************************************/
/*
 * Table 6-38 Passive Cable VDO
 * -------------------------------------------------------------
 * <31:28> : HW Version
 *           0000b..1111b assigned by the VID owner
 * <27:24> : Firmware Version
 *           0000b..1111b assigned by the VID owner
 * <23:21> : VDO Version
 *           Version Number of the VDO (not this specification Version):
 •           Version 1.0 = 000b
 *           Values 001b..111b are Reserved and Shall Not be used
 * <20>    : Reserved
 *           Shall be set to zero.
 * <19:18> : USB Type-C plug to USB TypeC/Captive
 *           00b = Reserved, Shall Not be used
 *           01b = Reserved, Shall Not be used
 *           10b = USB Type-C
 *           11b = Captive
 * <17>    : Reserved
 *           Shall be set to zero.
 * <16:13> : Cable Latency
 *           0000b – Reserved, Shall Not be used
 *           0001b – <10ns (~1m)
 *           0010b – 10ns to 20ns (~2m)
 *           0011b – 20ns to 30ns (~3m)
 *           0100b – 30ns to 40ns (~4m)
 *           0101b – 40ns to 50ns (~5m)
 *           0110b – 50ns to 60ns (~6m)
 *           0111b – 60ns to 70ns (~7m)
 *           1000b – > 70ns (>~7m)
 *           1001b..1111b Reserved, Shall Not be used
 *           Includes latency of electronics in Active Cable
 * <12:11> : Cable Termination Type
 *           00b = VCONN not required.
 *                 Cable Plugs that only support Discover Identity Commands
 *                 Shall set these bits to 00b.
 *           01b = VCONN required
 *           10b..11b = Reserved, Shall Not be used
 * <10:9>  : Maximum VBUS Voltage
 *           00b – 20V
 *           01b – 30V
 *           10b – 40V
 *           11b – 50V
 * <8:7>   : Reserved
 *           Shall be set to zero.
 * <6:5>   : VBUS Current Handling Capability
 *           00b = Reserved, Shall Not be used.
 *           01b = 3A
 *           10b = 5A
 *           11b = Reserved, Shall Not be used.
 * <4:3>   : Reserved
 *           Shall be set to zero.
 * <2:0>   : USB Highest Speed
 *           000b = [USB 2.0] only, no SuperSpeed support
 *           001b = [USB 3.2]/[USB4] Gen1
 *           010b = [USB 3.2]/[USB4] Gen2
 *           011b = [USB4] Gen3
 *           100b..111b = Reserved, Shall Not be used
 */

/*
 * Ref: USB Type-C Cable and Connector Specification 2.0
 * Table 5-1 Certified Cables Where USB4-compatible Operation is Expected
 * This table lists the USB-C cables those support USB4
 */
enum usb_rev30_plug {
	USB_REV30_TYPE_C = 2,
	USB_REV30_CAPTIVE = 3,
};

enum usb_rev30_latency {
	USB_REV30_LATENCY_1m = 1,
	USB_REV30_LATENCY_2m = 2,
	USB_REV30_LATENCY_3m = 3,
	USB_REV30_LATENCY_4m = 4,
	USB_REV30_LATENCY_5m = 5,
	USB_REV30_LATENCY_6m = 6,
};

enum usb_vbus_cur {
	USB_VBUS_CUR_RES_0,
	USB_VBUS_CUR_3A,
	USB_VBUS_CUR_5A,
	USB_VBUS_CUR_RES_3,
};

union passive_cable_vdo_rev30 {
	struct {
		enum usb_rev30_ss ss : 3;
		uint32_t reserved0 : 2;
		enum usb_vbus_cur vbus_cur : 2;
		uint32_t reserved1 : 2;
		uint32_t vbus_max : 2;
		uint32_t termination : 2;
		uint32_t latency : 4;
		uint32_t reserved2 : 1;
		uint32_t connector : 2;
		uint32_t reserved3 : 1;
		uint32_t vdo_version : 3;
		uint32_t fw_version : 4;
		uint32_t hw_version : 4;
	};
	uint32_t raw_value;
};

/* Macro passive VDO generator */
#define VDO_REV30_PASSIVE(ss, vbus_cur, latency, plug)                \
	((ss & 0x7) | (vbus_cur & 0x3) << 5 | (latency & 0xf) << 13 | \
	 (plug & 0x3) << 18)

/*****************************************************************************/
/*
 * Table 6-39 Active Cable VDO 1
 * -------------------------------------------------------------
 * <31:28> : HW Version 0000b..1111b assigned by the VID owner
 * <27:24> : Firmware Version 0000b..1111b assigned by the VID owner
 * <23:21> : VDO Version Version Number of the VDO
 *           (not this specification Version):
 •           Version 1.3 = 011b
 *           Values 000b, 100b..111b are Reserved and Shall Not be used
 * <20>    : Reserved Shall be set to zero.
 * <19:18> : Connector Type 00b = Reserved, Shall Not be used
 *           01b = Reserved, Shall Not be used
 *           10b = USB Type-C
 *           11b = Captive
 * <17>    : Reserved Shall be set to zero.
 * <16:13> : Cable Latency 0000b – Reserved, Shall Not be used
 *           0001b – <10ns (~1m)
 *           0010b – 10ns to 20ns (~2m)
 *           0011b – 20ns to 30ns (~3m)
 *           0100b – 30ns to 40ns (~4m)
 *           0101b – 40ns to 50ns (~5m)
 *           0110b – 50ns to 60ns (~6m)
 *           0111b – 60ns to 70ns (~7m)
 *           1000b –1000ns (~100m)
 *           1001b –2000ns (~200m)
 *           1010b – 3000ns (~300m)
 *           1011b..1111b Reserved, Shall Not be used
 *           Includes latency of electronics in Active Cable
 * <12:11> : Cable Termination Type 00b..01b = Reserved, Shall Not be used
 *           10b = One end Active, one end passive, VCONN required
 *           11b = Both ends Active, VCONN required
 * <10:9>  : Maximum VBUS Voltage Maximum Cable VBUS Voltage:
 *           00b – 20V
 *           01b – 30V
 *           10b – 40V
 *           11b – 50V
 * <8>     : SBU Supported
 *           0 = SBUs connections supported
 *           1 = SBU connections are not supported
 * <7>     : SBU Type
 *           When SBU Supported = 1 this bit Shall be Ignored
 *           When SBU Supported = 0:
 *           0 = SBU is passive
 *           1 = SBU is active
 * <6:5>   : VBUS Current Handling Capability
 *           When VBUS Through Cable is “No”, this field Shall be Ignored.
 *           When VBUS Though Cable is “Yes”:
 *           00b = USB Type-C Default Current
 *           01b = 3A
 *           10b = 5A
 *           11b = Reserved, Shall Not be used.
 * <4>     : VBUS Through Cable
 *           0 = No
 *           1 = Yes
 * <3>     : SOP” Controller Present
 *           0 = No SOP” controller present
 *           1 = SOP” controller present
 * <2:0>   : USB Highest Speed
 *           000b = [USB 2.0] only, no SuperSpeed support
 *           001b = [USB 3.2]/[USB4] Gen1
 *           010b = [USB 3.2]/[USB4] Gen2
 *           011b = [USB4] Gen3
 *           100b..111b = Reserved, Shall Not be used
 */

#define VDO_REV30_ACTIVE_1(ss, sop_pp, vbus_cable, vbus_cur, sbu_type,   \
			   sbu_sup, vbus_vol, cable_term, latency, plug) \
	((ss & 7) | (sop_pp & 0x1) << 3 | (vbus_cable & 0x1) << 4 |      \
	 (vbus_cur & 0x3) << 5 | (sbu_type & 0x1) << 7 |                 \
	 (sbu_sup & 0x1) << 8 | (vbus_vol & 0x3) << 0x9 |                \
	 (cable_term & 0x3) << 11 | (latency & 0xf) << 13 |              \
	 (plug & 0x3) << 18)

enum vdo_version {
	VDO_VERSION_1_3 = 3,
};

union active_cable_vdo1_rev30 {
	struct {
		enum usb_rev30_ss ss : 3;
		uint32_t sop_p_p : 1;
		uint32_t vbus_cable : 1;
		enum usb_vbus_cur vbus_cur : 2;
		uint32_t sbu_type : 1;
		uint32_t sbu_support : 1;
		uint32_t vbus_max : 2;
		uint32_t termination : 2;
		uint32_t latency : 4;
		uint32_t reserved0 : 1;
		uint32_t connector : 2;
		uint32_t reserved1 : 1;
		enum vdo_version vdo_ver : 3;
		uint32_t fw_version : 4;
		uint32_t hw_version : 4;
	};
	uint32_t raw_value;
};

/*****************************************************************************/
/*
 * Table 6-40 Active Cable VDO 2
 * -------------------------------------------------------------
 * <31:24> : Maximum Operating Temperature
 *           The maximum internal operating temperature.
 *           It may or may not reflect the plug’s skin temperature.
 * <23:16> : Shutdown Temperature
 *           The temperature at which the cable will go into thermal shutdown
 *           so as not to exceed the allowable plug skin temperature.
 * <15>    : Reserved Shall be set to zero.
 * <14:12> : U3/CLd Power 000b: >10mW
 *           001b: 5-10mW
 *           010b: 1-5mW
 *           011b: 0.5-1mW
 *           100b: 0.2-0.5mW
 *           101b: 50-200µW
 *           110b: <50µW
 *           111b: Reserved, Shall Not be used
 * <11>    : U3 to U0 transition mode
 *           0b: U3 to U0 direct
 *           1b: U3 to U0 through U3S
 * <10>    : Physical connection
 *           0b = Copper
 *           1b = Optical
 * <9>     : Active element
 *           0b = Active Redriver
 *           1b = Active Retimer
 * <8>     : USB4 Supported
 *           0b = [USB4] supported
 *           1b = [USB4] not supported
 * <7:6>   : USB 2.0 Hub Hops Consumed
 *           Number of [USB 2.0] ‘hub hops’ cable consumes.
 *           Shall be set to 0 if USB 2.0 not supported.
 * <5>     : USB 2.0 Supported
 *           0b = [USB 2.0] supported
 *           1b = [USB 2.0] not supported
 * <4>     : USB 3.2 Supported
 *           0b = [USB 3.2] SuperSpeed supported
 *           1b = [USB 3.2] SuperSpeed not supported
 * <3>     : USB Lanes Supported
 *           0b = One lane
 *           1b = Two lanes
 * <2>     : Optically Isolated Active Cable
 *           0b = No
 *           1b = Yes
 * <1>     : Reserved Shall be set to zero.
 * <0>     : USB Gen
 *           0b = Gen 1
 *           1b = Gen 2 or higher
 *           Note: see VDO1 USB Highest Speed for details of Gen supported.
 */

#define VDO_REV30_ACTIVE_2(gen, iso, lanes, usb32, usb2, usb2_hub, usb4,       \
			   active, optical, u3, u3_power, shutdown, max_temp)  \
	((gen & 0x1) | (iso & 0x1) << 2 | (lanes & 0x1) << 3 |                 \
	 (usb32 & 0x1) << 4 | (usb2 & 0x1) << 5 | (usb2_hub & 0x3) << 6 |      \
	 (usb4 & 0x1) << 8 | (active & 0x1) << 9 | (optical & 0x1) << 10 |     \
	 (u3 & 0x1) << 11 | (u3_power & 0x7) << 12 | (shutdown & 0x7f) << 16 | \
	 (max_temp & 0x7f) << 24)

enum retimer_active_element {
	ACTIVE_REDRIVER,
	ACTIVE_RETIMER,
};

enum active_cable_usb2_support {
	USB2_SUPPORTED,
	USB2_NOT_SUPPORTED,
};

enum active_cable_usb4_support {
	USB4_SUPPORTED,
	USB4_NOT_SUPPORTED,
};

union active_cable_vdo2_rev30 {
	struct {
		uint8_t usb_gen : 1;
		uint8_t reserved0 : 1;
		uint8_t a_cable_type : 1;
		uint8_t usb_lanes : 1;
		uint8_t usb_32_support : 1;
		enum active_cable_usb2_support usb_20_support : 1;
		uint8_t usb_20_hub_hop : 2;
		enum active_cable_usb4_support usb_40_support : 1;
		enum retimer_active_element active_elem : 1;
		uint8_t physical_conn : 1;
		uint8_t u3_to_u0 : 1;
		uint8_t u3_power : 3;
		uint8_t reserved1 : 1;
		uint8_t shutdown_temp : 8;
		uint8_t max_operating_temp : 8;
	};
	uint32_t raw_value;
};

/*****************************************************************************/
/*
 * Table 6-41 AMA VDO
 * -------------------------------------------------------------
 * <31:28> : HW Version
 *           0000b..1111b assigned by the VID owner
 * <27:24> : Firmware Version
 *           0000b..1111b assigned by the VID owner
 * <23:21> : VDO Version
 *           Version Number of the VDO (not this specification Version):
 *           Version 1.0 = 000b
 *           Values 001b..111b are Reserved and Shall Not be used
 * <20:8>  : Reserved. Shall be set to zero.
 * <7:5>   : VCONN power
 *           When the VCONN required field is set to “Yes” VCONN power
 *           needed by adapter for full functionality
 *           000b = 1W
 *           001b = 1.5W
 *           010b = 2W
 *           011b = 3W
 *           100b = 4W
 *           101b = 5W
 *           110b = 6W
 *           111b = Reserved, Shall Not be used
 *           When the VCONN required field is set to “No” Reserved,
 *           Shall be set to zero.
 * <4>     : VCONN required
 *           0 = No
 *           1 = Yes
 * <3>     : VBUS required
 *           0 = No
 *           1 = Yes
 * <2:0>   : USB Highest Speed
 *           000b = [USB 2.0] only
 *           001b = [USB 3.2] Gen1 and USB 2.0
 *           010b = [USB 3.2] Gen1, Gen2 and USB 2.0
 *           011b = [USB 2.0] billboard only
 *           100b..111b = Reserved, Shall Not be used
 */

/*****************************************************************************/
/*
 * Table 6-42 VPD VDO
 * -------------------------------------------------------------
 * <31:28> : HW Version
 *           0000b..1111b assigned by the VID owner
 * <27:24> : Firmware Version
 *           0000b..1111b assigned by the VID owner
 * <23:21> : VDO Version Version Number of the VDO
 *           (not this specification Version):
 *           Version 1.0 = 000b
 *           Values 001b…111b are Reserved and Shall Not be used
 * <20:17> : Reserved Shall be set to zero.
 * <16:15> : Maximum VBUS Voltage
 *           Maximum Cable VBUS Voltage:
 *           00b – 20V
 *           01b – 30V
 *           10b – 40V
 *           11b – 50V
 * <14>    : Charge Through Current Support
 *           Charge Through Support bit=1b:
 *           0b - 3A capable;
 *           1b - 5A capable
 *           Charge Through Support bit = 0b: Reserved, Shall be set to zero
 * <13>    : Reserved Shall be set to zero.
 * <12:7>  : VBUS Impedance
 *           Charge Through Support bit = 1b:
 *           Vbus impedance through the VPD in 2 mΩ increments.
 *           Values less than 10 mΩ are Reserved and Shall Not be used.
 *           Charge Through Support bit = 0b: Reserved, Shall be set to zero
 * <6:1>   : Ground Impedance
 *           Charge Through Support bit = 1b:
 *           Ground impedance through the VPD in 1 mΩ increments.
 *           Values less than 10 mΩ are Reserved and Shall Not be used.
 *           Charge Through Support bit = 0b: Reserved, Shall be set to zero
 * <0>     : Charge Through Support
 *           1b – the VPD supports Charge Through
 *           0b – the VPD does not support Charge Through
 */
#define VDO_VPD(hw, fw, vbus, ctc, vbusz, gndz, cts)                          \
	(((hw) & 0xf) << 28 | ((fw) & 0xf) << 24 | ((vbus) & 0x3) << 15 |     \
	 ((ctc) & 0x1) << 14 | ((vbusz) & 0x3f) << 7 | ((gndz) & 0x3f) << 1 | \
	 (cts))

enum vpd_ctc_support { VPD_CT_CURRENT_3A, VPD_CT_CURRENT_5A };

enum vpd_vbus {
	VPD_MAX_VBUS_20V,
	VPD_MAX_VBUS_30V,
	VPD_MAX_VBUS_40V,
	VPD_MAX_VBUS_50V,
};

enum vpd_cts_support {
	VPD_CTS_NOT_SUPPORTED,
	VPD_CTS_SUPPORTED,
};

#define VPD_VDO_MAX_VBUS(vdo) (((vdo) >> 15) & 0x3)
#define VPD_VDO_CURRENT(vdo) (((vdo) >> 14) & 1)
#define VPD_VDO_VBUS_IMP(vdo) (((vdo) >> 7) & 0x3f)
#define VPD_VDO_GND_IMP(vdo) (((vdo) >> 1) & 0x3f)
#define VPD_VDO_CTS(vdo) ((vdo) & 1)
#define VPD_VBUS_IMP(mo) ((mo + 1) >> 1)
#define VPD_GND_IMP(mo) (mo)

/*
 * ############################################################################
 *
 * Reference: USB Power Delivery Specification Revision 2.0, Version 1.3
 *
 * ############################################################################
 */

/*****************************************************************************/
/*
 * Table 6-23 ID Header VDO
 *
 * Note: PD 3.0 ID header (Table 6-29, PD Revision 3.0 Spec) makes use of
 * reserved bits 25:21 for a connector type and product type (DFP).  It is not
 * advised to create a structure using these bits however, as the DFP product
 * type crosses a byte boundary and causes problems with gcc's structure
 * alignment.
 * -------------------------------------------------------------
 * <31>    : USB Communications Capable as USB Host
 * <30>    : USB Communications Capable as a USB Device
 * <29:27> : Product Type (UFP):
 *           000b = Undefined
 *           001b = PDUSB Hub
 *           010b = PDUSB Peripheral
 *           011b = PSD (PD 3.0)
 *           101b = Alternate Mode Adapter (AMA) - deprecated in PD r3.1
 *           110b = Vconn-Powered USB Device (VPD, PD 3.0)
 *           Product Type (Cable Plug):
 *           000b = Undefined
 *           011b = Passive Cable
 *           100b = Active Cable
 * <26>    : Modal Operation Supported
 * <25:16> : Reserved
 * <15:0>  : USB Vendor ID
 */
enum idh_ptype {
	IDH_PTYPE_UNDEF,
	IDH_PTYPE_HUB,
	IDH_PTYPE_PERIPH,
	IDH_PTYPE_PSD = 3,
	IDH_PTYPE_PCABLE = 3,
	IDH_PTYPE_ACABLE,
	IDH_PTYPE_AMA,
	IDH_PTYPE_VPD,
};

/*
 * Product type for UFP shall be either Hub or peripheral or PSD or AMA or VDP
 * Reference:
 * - Table 6-29 ID Header VDO PD spec 3.0 version 2.0 and
 * - Table 6-23 ID Header VDO PD spec 2.0 version 1.3.
 */
#define IS_PD_IDH_UFP_PTYPE(ptype)                              \
	(ptype == IDH_PTYPE_HUB || ptype == IDH_PTYPE_PERIPH || \
	 ptype == IDH_PTYPE_PSD || ptype == IDH_PTYPE_AMA ||    \
	 ptype == IDH_PTYPE_VPD)

struct id_header_vdo_rev20 {
	uint16_t usb_vendor_id;
	uint16_t reserved0 : 10;
	uint8_t modal_support : 1;
	enum idh_ptype product_type : 3;
	uint8_t usb_device : 1;
	uint8_t usb_host : 1;
};

/*****************************************************************************/
/*
 * Table 6-28 Passive Cable VDO
 * -------------------------------------------------------------
 * <31:28> : HW Version
 *           0000b..1111b assigned by the VID owner
 * <27:24> : Firmware Version
 *           0000b..1111b assigned by the VID owner
 * <23:20> : Reserved
 *           Shall be set to zero.
 * <19:18> : USB Type-C plug to USB Type-A/B/C/Captive
 *           00b = USB Type-A
 *           01b = USB Type-B
 *           10b = USB Type-C
 *           11b = Captive
 * <17>    : Reserved
 *           Shall be set to zero.
 * <16:13> : Cable Latency
 *           0000b – Reserved, Shall Not be used
 *           0001b – <10ns (~1m)
 *           0010b – 10ns to 20ns (~2m)
 *           0011b – 20ns to 30ns (~3m)
 *           0100b – 30ns to 40ns (~4m)
 *           0101b – 40ns to 50ns (~5m)
 *           0110b – 50ns to 60ns (~6m)
 *           0111b – 60ns to 70ns (~7m)
 *           1000b – > 70ns (>~7m)
 *           1001b..1111b Reserved, Shall Not be used
 *           Includes latency of electronics in Active Cable
 * <12:11> : Cable Termination Type
 *           00b = VCONN not required. Cable Plugs that only support
 *                 Discover Identity Commands Shall set these bits to 00b.
 *           01b = VCONN required
 *           10b..11b = Reserved, Shall Not be used
 * <10>    : SSTX1 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <9>     : SSTX2 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <8>     : SSRX1 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <7>     : SSRX2 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <6:5>   : VBUS Current Handling Capability
 *           00b = Reserved, Shall Not be used.
 *           01b = 3A
 *           10b = 5A
 *           11b = Reserved, Shall Not be used.
 * <4>     : VBUS through cable
 *           0 = No
 *           1 = Yes
 * <3>     : Reserved Shall be set to 0.
 * <2:0>   : USB SuperSpeed Signaling Support
 *           000b = USB 2.0 only, no SuperSpeed support
 *           001b = [USB 3.1] Gen1
 *           010b = [USB 3.1] Gen1 and Gen2
 *           011b..111b = Reserved, Shall Not be used
 *           See [USB Type-C 1.2] for definitions.
 */
enum usb_rev20_ss {
	USB_R20_SS_U2_ONLY,
	USB_R20_SS_U31_GEN1,
	USB_R20_SS_U31_GEN1_GEN2,
	USB_R20_SS_RES_3,
	USB_R20_SS_RES_4,
	USB_R20_SS_RES_5,
	USB_R20_SS_RES_6,
	USB_R20_SS_RES_7,
};

union passive_cable_vdo_rev20 {
	struct {
		enum usb_rev20_ss ss : 3;
		uint32_t reserved0 : 1;
		uint32_t vbus_cable : 1;
		enum usb_vbus_cur vbus_cur : 2;
		uint32_t ssrx2 : 1;
		uint32_t ssrx1 : 1;
		uint32_t sstx2 : 1;
		uint32_t sstx1 : 1;
		uint32_t termination : 2;
		uint32_t latency : 4;
		uint32_t reserved1 : 1;
		uint32_t connector : 2;
		uint32_t reserved2 : 4;
		uint32_t fw_version : 4;
		uint32_t hw_version : 4;
	};
	uint32_t raw_value;
};

/*****************************************************************************/
/*
 * Table 6-29 Active Cable VDO
 * -------------------------------------------------------------
 * <31:28> : HW Version
 *           0000b..1111b assigned by the VID owner
 * <27:24> : Firmware Version
 *           0000b..1111b assigned by the VID owner
 * <23:20> : Reserved
 *           Shall be set to zero.
 * <19:18> : USB Type-C plug to USB Type-A/B/C/Captive
 *           00b = USB Type-A
 *           01b = USB Type-B
 *           10b = USB Type-C
 *           11b = Captive
 * <17>    : Reserved
 *           Shall be set to zero.
 * <16:13> : Cable Latency
 *           0000b – Reserved, Shall Not be used
 *           0001b – <10ns (~1m)
 *           0010b – 10ns to 20ns (~2m)
 *           0011b – 20ns to 30ns (~3m)
 *           0100b – 30ns to 40ns (~4m)
 *           0101b – 40ns to 50ns (~5m)
 *           0110b – 50ns to 60ns (~6m)
 *           0111b – 60ns to 70ns (~7m)
 *           1000b – > 70ns (>~7m)
 *           1001b..1111b Reserved, Shall Not be used
 *           Includes latency of electronics in Active Cable
 * <12:11> : Cable Termination Type
 *           00b..01b = Reserved, Shall Not be used
 *           10b = One end Active, one end passive, VCONN required
 *           11b = Both ends Active, VCONN required
 * <10>    : SSTX1 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <9>     : SSTX2 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <8>     : SSRX1 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <7>     : SSRX2 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <6:5>   : VBUS Current Handling Capability
 *           00b = Reserved, Shall Not be used.
 *           01b = 3A
 *           10b = 5A
 *           11b = Reserved, Shall Not be used.
 * <4>     : VBUS through cable
 *           0 = No
 *           1 = Yes
 * <3>     : SOP” controller present?
 *           1 = SOP” controller present
 *           0 = No SOP” controller present
 * <2:0>   : USB SuperSpeed Signaling Support
 *           000b = USB 2.0 only, no SuperSpeed support
 *           001b = [USB 3.1] Gen1
 *           010b = [USB 3.1] Gen1 and Gen2
 *           011b..111b = Reserved, Shall Not be used
 */
union active_cable_vdo_rev20 {
	struct {
		enum usb_rev20_ss ss : 3;
		uint32_t sop_p_p : 1;
		uint32_t vbus_cable : 1;
		enum usb_vbus_cur vbus_cur : 2;
		uint32_t ssrx2 : 1;
		uint32_t ssrx1 : 1;
		uint32_t sstx2 : 1;
		uint32_t sstx1 : 1;
		uint32_t termination : 2;
		uint32_t latency : 4;
		uint32_t reserved0 : 1;
		uint32_t connector : 2;
		uint32_t reserved1 : 1;
		uint32_t vdo_version : 3;
		uint32_t fw_version : 4;
		uint32_t hw_version : 4;
	};
	uint32_t raw_value;
};

/*****************************************************************************/
/*
 * Table 6-30 AMA VDO
 * -------------------------------------------------------------
 * <31:28> : HW Version
 *           0000b..1111b assigned by the VID owner
 * <27:24> : Firmware Version
 *           0000b..1111b assigned by the VID owner
 * <23:12> : Reserved
 *           Shall be set to zero.
 * <11>    : SSTX1 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <10>     : SSTX2 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <9>     : SSRX1 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <8>     : SSRX2 Directionality Support
 *           0 = Fixed
 *           1 = Configurable
 * <7:5>   : VCONN power
 *           When the VCONN required field is set to “Yes” VCONN power
 *           needed by adapter for full functionality
 *           000b = 1W
 *           001b = 1.5W
 *           010b = 2W
 *           011b = 3W
 *           100b = 4W
 *           101b = 5W
 *           110b = 6W
 *           111b = Reserved, Shall Not be used
 *           When the VCONN required field is set to “No” Reserved,
 *           Shall be set to zero.
 * <4>     : VCONN required
 *           0 = No
 *           1 = Yes
 * <3>     : VBUS required
 *           0 = No
 *           1 = Yes
 * <2:0>   : USB Highest Speed
 *           000b = [USB 2.0] only
 *           001b = [USB 3.2] Gen1 and USB 2.0
 *           010b = [USB 3.2] Gen1, Gen2 and USB 2.0
 *           011b = [USB 2.0] billboard only
 *           100b..111b = Reserved, Shall Not be used
 */
#define VDO_AMA(hw, fw, tx1d, tx2d, rx1d, rx2d, vcpwr, vcr, vbr, usbss)    \
	(((hw) & 0x7) << 28 | ((fw) & 0x7) << 24 | (tx1d) << 11 |          \
	 (tx2d) << 10 | (rx1d) << 9 | (rx2d) << 8 | ((vcpwr) & 0x3) << 5 | \
	 (vcr) << 4 | (vbr) << 3 | ((usbss) & 0x7))

#define PD_VDO_AMA_VCONN_REQ(vdo) (((vdo) >> 4) & 1)
#define PD_VDO_AMA_VBUS_REQ(vdo) (((vdo) >> 3) & 1)

enum ama_usb_ss {
	AMA_USBSS_U2_ONLY,
	AMA_USBSS_U31_GEN1,
	AMA_USBSS_U31_GEN2,
	AMA_USBSS_BBONLY,
};

/*
 * Enter USB Data Object (Ref: USB PD 3.2 Version 2.0 Table 6-47)
 * -----------------------
 * <31>    : Reserved
 * <30:28> : USB Mode
 *           000b - USB2.0
 *           001b - USB3.2
 *           010b - USB4
 * <27>    : Reserved
 * <26>    : USB4 DRD
 *           0b: Not capable of operating as a [USB4] Device
 *           1b: Capable of operating as a [USB4] Device
 * <25>    : USB3 DRD
 *           0b: Not capable of operating as a [USB 3.2] Device
 *           1b: Capable of operating as a [USB 3.2] Device
 * <24>    : Reserved
 * <23:21> : Cable Speed
 *           000b - [USB 2.0] only, no SuperSpeed support
 *           001b - [USB 3.2] Gen1
 *           010b - [USB 3.2] Gen2 and [USB4] Gen2
 *           011b - [USB4] Gen3
 *           111b..100b: Reserved, Shall not be used
 * <20:19> : Cable Type
 *           00b - Passive
 *           01b - Active Re-timer
 *           10b - Active Re-driver
 *           11b - Optically Isolated
 * <18:17> : Cable Current
 *           00b = VBUS is not supported
 *           01b = Reserved
 *           10b = 3A
 *           11b = 5A
 * <16>    : PCIe Supported ? (1b == Yes, 0b == No)
 * <15     : DP Supported ? (1b == Yes, 0b == No)
 * <14>    : TBT Supported ? (1b == Yes, 0b == No)
 * <13>    : Host present ? (1b == Yes, 0b == No)
 * <12:0>  : Reserved
 */
enum usb_mode {
	USB_PD_20,
	USB_PD_32,
	USB_PD_40,
	USB_PD_INVALID_3,
	USB_PD_INVALID_4,
	USB_PD_INVALID_5,
	USB_PD_INVALID_6,
	USB_PD_INVALID_7,
};

enum usb4_cable_current {
	USB4_CABLE_CURRENT_INVALID,
	USB4_CABLE_CURRENT_RESERVED,
	USB4_CABLE_CURRENT_3A,
	USB4_CABLE_CURRENT_5A,
};

enum usb4_cable_type {
	CABLE_TYPE_PASSIVE,
	CABLE_TYPE_ACTIVE_RETIMER,
	CABLE_TYPE_ACTIVE_REDRIVER,
	CABLE_TYPE_ISOLATED,
};

union enter_usb_data_obj {
	struct {
		uint16_t reserved3 : 13;
		uint8_t host_present : 1;
		uint8_t tbt_supported : 1;
		uint8_t dp_supported : 1;
		uint8_t pcie_supported : 1;
		enum usb4_cable_current cable_current : 2;
		enum usb4_cable_type cable_type : 2;
		enum usb_rev30_ss cable_speed : 3;
		uint8_t reserved2 : 1;
		uint8_t usb3_drd_cap : 1;
		uint8_t usb4_drd_cap : 1;
		uint8_t reserved1 : 1;
		enum usb_mode mode : 3;
		uint8_t reserved0 : 1;
	};
	uint32_t raw_value;
};

union vpd_vdo {
	struct {
		uint32_t ct_support : 1;
		uint32_t gnd_impedance : 6;
		uint32_t vbus_impedance : 6;
		uint32_t reserved0 : 1;
		uint32_t ct_current_support : 1;
		uint32_t max_vbus_voltage : 2;
		uint32_t reserved1 : 4;
		uint32_t vdo_version : 3;
		uint32_t firmware_version : 4;
		uint32_t hw_version : 4;
	};
	uint32_t raw_value;
};

/*
 * ############################################################################
 *
 * Unions of VDOs which differ based on revision or type
 *
 * ############################################################################
 */

union product_type_vdo1 {
	/* Passive cable VDO */
	union passive_cable_vdo_rev20 p_rev20;
	union passive_cable_vdo_rev30 p_rev30;

	/* Active cable VDO */
	union active_cable_vdo_rev20 a_rev20;
	union active_cable_vdo1_rev30 a_rev30;

	/* Vconn Power USB Device VDO */
	union vpd_vdo vpd;

	uint32_t raw_value;
};
BUILD_ASSERT(sizeof(uint32_t) == sizeof(union product_type_vdo1));

union product_type_vdo2 {
	union active_cable_vdo2_rev30 a2_rev30;

	uint32_t raw_value;
};
BUILD_ASSERT(sizeof(uint32_t) == sizeof(union product_type_vdo2));

#endif /* __CROS_EC_USB_PD_VDO_H */
