/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * DisplayPort alternate mode support
 * Refer to VESA DisplayPort Alt Mode on USB Type-C Standard, version 2.0,
 * section 5.2
 */

#ifndef __CROS_EC_USB_DP_ALT_MODE_H
#define __CROS_EC_USB_DP_ALT_MODE_H

#include "config.h"
#include "tcpm/tcpm.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_vdo.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reference: VESA DisplayPort Alt Mode on USB Type-C Standard Version 2.1 */
enum dpam_version {
	DPAM_VERSION_20,
	DPAM_VERSION_21,
};

enum dp_bit_rate {
	DP_HBR3 = BIT(0),
	DP_UHBR10 = BIT(1),
	DP_UHBR20 = BIT(2),
};

enum dp21_cable_type {
	DP21_PASSIVE_CABLE = 0,
	DP21_ACTIVE_RETIMER_CABLE,
	DP21_ACTIVE_REDRIVER_CABLE,
	DP21_OPTICAL_CABLE,
};

enum dp_config {
	DP_USB_ONLY = 0,
	DP_SOURCE,
	DP_SINK,
};

struct dp_cable_type_flags {
	bool active;
	bool retimer;
	bool optical;
};

/*
 * Table 4-4: SOP' Cable DP Capabilities
 * ------------------------------------------------------------------
 * <1:0>   : reserved
 * <5:2>   : signaling : XXX1b == HBR3, XX1Xb == UHBR10, X1XXb == UHBR20
 * <7:6>   : reserved
 * <15:8>  : DFP_D pin assignment supported
 * <23:16> : UFP_D pin assignment supported
 * <25:24> : reserved
 * <26>    : UHBR13.5 Support
 * <27>    : reserved
 * <29:28> : active comp : 0h == Passive, 1h == Active ReTimer
 *                        2h == Active ReDriver, 3h == Optical
 * <31:30> : DPAM Version
 */

union dp_mode_resp_cable {
	struct {
		unsigned int reserved1 : 2;
		unsigned int signaling : 4;
		unsigned int reserved2 : 2;
		unsigned int dfp_d_pin : 8;
		unsigned int ufp_d_pin : 8;
		unsigned int reserved3 : 2;
		unsigned int uhbr13_5_support : 1;
		unsigned int reserved4 : 1;
		enum dp21_cable_type active_comp : 2;
		enum dpam_version dpam_ver : 2;
	};
	uint32_t raw_value;
};

/*
 * Table 5-13: SOP DisplayPort Configurations
 * ------------------------------------------------------------------
 * <1:0>   : cfg : 00 == USB, 01 == DFP_D, 10 == UFP_D, 11 == reserved
 * <5:2>   : signaling : XXX1b == HBR3, XX1Xb == UHBR10, X1XXb == UHBR20
 *           Other bits are reserved for higher bit rate.
 * <7:6>   : reserved
 * <15:8>  : DFP_D pin assignment supported
 * <23:16> : UFP_D pin assignment supported
 * <25:24> : reserved
 * <26>    : UHBR13.5 Support
 * <27>    : reserved
 * <29:28> : cable type : 0h == Passive, 1h == Active ReTimer
 *                        2h == Active ReDriver, 3h == Optical
 * <31:30> : DPAM Version
 */

union dp_mode_cfg {
	struct {
		unsigned int cfg : 2;
		unsigned int signaling : 4;
		unsigned int reserved1 : 2;
		unsigned int dfp_d_pin : 8;
		unsigned int ufp_d_pin : 8;
		unsigned int reserved2 : 2;
		unsigned int uhbr13_5_support : 1;
		unsigned int reserved3 : 1;
		enum dp21_cable_type active_comp : 2;
		enum dpam_version dpam_ver : 2;
	};
	uint32_t raw_value;
};
BUILD_ASSERT(sizeof(union dp_mode_cfg) == sizeof(uint32_t));

#define VDM_VERS_MINOR \
	(IS_ENABLED(CONFIG_USB_PD_DP21_MODE) ? VDO_SVDM_VERS_MINOR(1) : 0)

#ifdef CONFIG_USB_PD_DP_MODE
/**
 * Resolves DPAM version
 *
 * @param port	The PD port number
 * @param type	Transmit type (SOP, SOP') for VDM
 * @return	DPAM_VERSION_20/DPAM_VERSION_21
 */
enum dpam_version dp_resolve_dpam_version(int port, enum tcpci_msg_type type);

/**
 * Resolves SVDM version from discovered DP capabilities
 *
 * @param port	The PD port number
 * @param type	Transmit type (SOP, SOP') for VDM
 * @return	SVDM_VER_2_0/SVDM_VER_2_1
 */
enum usb_pd_svdm_ver dp_resolve_svdm_version(int port,
					     enum tcpci_msg_type type);

/**
 * Get Cable speed
 *
 * @param port	The PD port number
 * @return	cable speed
 */
enum dp_bit_rate dp_get_cable_bit_rate(int port);

/**
 * Check DP Mode entry allowed
 * If DP 2.1 not supported returns true
 * If DP 2.1 is supported follows Fig 5-3 of DP 2.1 Spec to decide if DPAM is
 * allowed
 *
 * @param port	The PD port number
 * @return	true/false
 */
bool dp_mode_entry_allowed(int port);

/**
 * Get Mode VDO data for DisplayPort svid
 *
 * @param port	The PD port number
 * @param type	Transmit type (SOP, SOP') for VDM
 * @return	Mode VDO
 */
uint32_t dp_get_mode_vdo(int port, enum tcpci_msg_type type);

/**
 * Combines the following information into struct
 * Active/Passive cable
 * Retimer/Redriver cable
 * Optical/Non-optical cable
 *
 * @param port	The PD port number
 * @return struct containing cable flags
 */
struct dp_cable_type_flags dp_get_pd_cable_type_flags(int port);

/**
 * Get Board allows UHBR13.5 entry
 *
 * @param port	The PD port number
 * @return	false - UHBR13.5 is not allowed, true - UHBR13.5 allowed
 */
__overridable bool board_is_dp_uhbr13_5_allowed(int port);

/**
 * Get UHBR13.5 is supported
 *
 * @param port	The PD port number
 * @return	false - UHBR13.5 Not supported, true - UHBR13.5 Supported
 */
bool dp_is_uhbr13_5_supported(int port);

/*
 * Initialize DP state for the specified port.
 *
 * @param port USB-C port number
 */
void dp_init(int port);

/*
 * Returns True if DisplayPort mode is in active state
 *
 * @param port      USB-C port number
 * @return          True if DisplayPort mode is in active state
 *                  False otherwise
 */
bool dp_is_active(int port);

/*
 * Returns True if DisplayPort mode entry has not started, or mode exit has
 * already finished.
 * TODO(b/235984702): Consolidate the DP state API
 *
 * @param port      USB-C port number
 * @return          True if DisplayPort mode is in inactive state
 *                  False otherwise.
 */
bool dp_is_idle(int port);

/*
 * Checks whether the mode entry sequence for DisplayPort alternate mode is done
 * for a port.
 *
 * @param port      USB-C port number
 * @return          True if entry sequence for DisplayPort mode is completed
 *                  False otherwise
 */
bool dp_entry_is_done(int port);

/*
 * Handles received DisplayPort VDM ACKs.
 *
 * @param port      USB-C port number
 * @param type      Transmit type (SOP, SOP') for received ACK
 * @param vdo_count The number of VDOs in the ACK VDM
 * @param vdm       VDM from ACK
 */
void dp_vdm_acked(int port, enum tcpci_msg_type type, int vdo_count,
		  uint32_t *vdm);

/*
 * Handles NAKed (or Not Supported or timed out) DisplayPort VDM requests.
 *
 * @param port    USB-C port number
 * @param type    Transmit type (SOP, SOP') for request
 * @param svid    The SVID of the request
 * @param vdm_cmd The VDM command of the request
 */
void dp_vdm_naked(int port, enum tcpci_msg_type type, uint8_t vdm_cmd);

/*
 * Construct the next DisplayPort VDM that should be sent.
 *
 * @param[in] port	    USB-C port number
 * @param[in,out] vdo_count The number of VDOs in vdm; must be at least
 *			    VDO_MAX_SIZE.  On success, number of populated VDOs
 * @param[out] vdm          The VDM payload to be sent; output; must point to at
 *			    least VDO_MAX_SIZE elements
 * @return		    enum dpm_msg_setup_status
 */
enum dpm_msg_setup_status dp_setup_next_vdm(int port, int *vdo_count,
					    uint32_t *vdm);
/*
 * Construct the vdo cfg message for the dp port
 *
 * @param port		USB-C port number
 * @param pin_mode	pin mode of the port
 * @return	returns dp_mode_cfg with appropriate values
 */
union dp_mode_cfg dp_create_vdo_cfg(int port, uint8_t pin_mode);

#else /* CONFIG_USB_PD_DP_MODE */
static inline void dp_init(int port)
{
}

static inline bool dp_is_active(int port)
{
	return false;
}

static inline bool dp_is_idle(int port)
{
	return true;
}

static inline bool dp_entry_is_done(int port)
{
	return false;
}

static inline void dp_vdm_acked(int port, enum tcpci_msg_type type,
				int vdo_count, uint32_t *vdm)
{
}

static inline void dp_vdm_naked(int port, enum tcpci_msg_type type,
				uint8_t vdm_cmd)
{
}

static inline enum dpm_msg_setup_status
dp_setup_next_vdm(int port, int *vdo_count, uint32_t *vdm)
{
	return MSG_SETUP_ERROR;
}

static inline bool dp_mode_entry_allowed(int port)
{
	return false;
}

static inline uint32_t dp_get_mode_vdo(int port, enum tcpci_msg_type type)
{
	return 0;
}

static inline enum dpam_version
dp_resolve_dpam_version(int port, enum tcpci_msg_type type)
{
	return DPAM_VERSION_20;
}

static inline enum dp_bit_rate dp_get_cable_bit_rate(int port)
{
	return DP_HBR3;
}

#endif /* CONFIG_USB_PD_DP_MODE */

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_USB_DP_ALT_MODE_H */
