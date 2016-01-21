/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB mux driver */

#ifndef __CROS_EC_USB_MUX_H
#define __CROS_EC_USB_MUX_H

#include "usb_charge.h"
#include "usb_pd.h"

/* USB-C mux state */
typedef uint8_t mux_state_t;

/* Mux state attributes */
#define MUX_USB_ENABLED        (1 << 0) /* USB is enabled */
#define MUX_DP_ENABLED         (1 << 1) /* DP is enabled */
#define MUX_POLARITY_INVERTED  (1 << 2) /* Polarity is inverted */

/* Mux modes, decoded to attributes */
enum typec_mux {
	TYPEC_MUX_NONE = 0,                /* Open switch */
	TYPEC_MUX_USB  = MUX_USB_ENABLED,  /* USB only */
	TYPEC_MUX_DP   = MUX_DP_ENABLED,   /* DP only */
	TYPEC_MUX_DOCK = MUX_USB_ENABLED | /* Both USB and DP */
			 MUX_DP_ENABLED,
};

/* Mux driver function pointers */
struct usb_mux_driver {
	/**
	 * Initialize USB mux.
	 *
	 * @param port_addr Port/address driver-defined parameter.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*init)(int port_addr);

	/**
	 * Set USB mux state.
	 *
	 * @param port_addr Port/address driver-defined parameter.
	 * @param mux_state State to set mux to.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*set)(int port_addr, mux_state_t mux_state);

	/**
	 * Get current state of USB mux.
	 *
	 * @param port_addr Port / address driver-defined parameter.
	 * @param mux_state Gets set to current state of mux.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*get)(int port_addr, mux_state_t *mux_state);
};

/* Describes a USB mux present in the system */
struct usb_mux {
	/*
	 * Driver-defined parameter, typically an i2c slave address
	 * (for i2c muxes) or a port number (for GPIO 'muxes').
	 */
	const int port_addr;
	/* Mux driver */
	const struct usb_mux_driver *driver;

	/**
	 * Board specific initialization for USB mux that is
	 * called after mux->driver->init() function.
	 *
	 * @param mux USB mux to tune
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*board_init)(const struct usb_mux *mux);
};

/* Supported USB mux drivers */
extern const struct usb_mux_driver pi3usb30532_usb_mux_driver;
extern const struct usb_mux_driver ps8740_usb_mux_driver;
extern const struct usb_mux_driver tcpm_usb_mux_driver;

/* USB muxes present in system, ordered by PD port #, defined at board-level */
extern struct usb_mux usb_muxes[];

/**
 * Initialize USB mux to its default state.
 *
 * @param port  Port number.
 */
void usb_mux_init(int port);

/**
 * Configure superspeed muxes on type-C port.
 *
 * @param port port number.
 * @param mux_mode mux selected function.
 * @param usb_config usb2.0 selected function.
 * @param polarity plug polarity (0=CC1, 1=CC2).
 */
void usb_mux_set(int port, enum typec_mux mux_mode,
		 enum usb_switch usb_config, int polarity);

/**
 * Query superspeed mux status on type-C port.
 *
 * @param port port number.
 * @param dp_str pointer to the DP string to return.
 * @param usb_str pointer to the USB string to return.
 * @return Non-zero if superspeed connection is enabled; otherwise, zero.
 */
int usb_mux_get(int port, const char **dp_str, const char **usb_str);

/**
 * Flip the superspeed muxes on type-C port.
 *
 * This is used for factory test automation. Note that this function should
 * only flip the superspeed muxes and leave CC lines alone. Without further
 * changes, this function MUST ONLY be used for testing purpose, because
 * the protocol layer loses track of the superspeed polarity and DP/USB3.0
 * connection may break.
 *
 * @param port port number.
 */
void usb_mux_flip(int port);
#endif
