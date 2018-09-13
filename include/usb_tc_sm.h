/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Type-C module */

#ifndef __CROS_EC_USB_TC_H
#define __CROS_EC_USB_TC_H

enum typec_state_id {
	DISABLED,
	UNATTACHED_SNK,
	ATTACH_WAIT_SNK,
	ATTACHED_SNK,
#if !defined(CONFIG_USB_TYPEC_VPD)
	ERROR_RECOVERY,
	UNATTACHED_SRC,
	ATTACH_WAIT_SRC,
	ATTACHED_SRC,
#endif
#if !defined(CONFIG_USB_TYPEC_CTVPD) && !defined(CONFIG_USB_TYPEC_VPD)
	AUDIO_ACCESSORY,
	ORIENTED_DEBUG_ACCESSORY_SRC,
	UNORIENTED_DEBUG_ACCESSORY_SRC,
	DEBUG_ACCESSORY_SNK,
	TRY_SRC,
	TRY_WAIT_SNK,
	CTUNATTACHED_SNK,
	CTATTACHED_SNK,
#endif
#if defined(CONFIG_USB_TYPEC_CTVPD)
	CTTRY_SNK,
	CTATTACHED_UNSUPPORTED,
	CTATTACH_WAIT_UNSUPPORTED,
	CTUNATTACHED_UNSUPPORTED,
	CTUNATTACHED_VPD,
	CTATTACH_WAIT_VPD,
	CTATTACHED_VPD,
	CTDISABLED_VPD,
	TRY_SNK,
	TRY_WAIT_SRC,
#endif
	/* Number of states. Not an actual state. */
	TC_STATE_COUNT,
};

/**
 * Get the id of the current Type-C state
 *
 * @param port USB-C port number
 */
enum typec_state_id get_typec_state_id(int port);

/**
 * Get current data role
 *
 * @param port USB-C port number
 * @return 0 for ufp, 1 for dfp, 2 for disconnected
 */
int tc_get_data_role(int port);

/**
 * Get current power role
 *
 * @param port USB-C port number
 * @return 0 for sink, 1 for source or vpd
 */
int tc_get_power_role(int port);

/**
 * Set loop timeout value
 *
 * @param port USB-C port number
 * @timeout time in ms
 */
void tc_set_timeout(int port, uint64_t timeout);

#ifdef CONFIG_USB_TYPEC_CTVPD
/**
 * Resets the charge-through support timer. This can be
 * called many times but the support timer will only
 * reset once, while in the Attached.SNK state.
 *
 * @param port USB-C port number
 */
void tc_reset_support_timer(int port);

#endif /* CONFIG_USB_TYPEC_CTVPD */
#endif /* __CROS_EC_USB_TC_H */

