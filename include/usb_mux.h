/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB mux driver */

#ifndef __CROS_EC_USB_MUX_H
#define __CROS_EC_USB_MUX_H

#include "ec_commands.h"
#include "tcpm.h"
#include "usb_charge.h"
#include "usb_pd.h"

/*
 * USB-C mux state
 *
 * A bitwise combination of the USB_PD_MUX_* flags.
 * The bottom 2 bits also correspond to the typec_mux enum type.
 */
typedef uint8_t mux_state_t;

/*
 * Packing and Unpacking defines used with USB_MUX_FLAG_NOT_TCPC
 * MUX_PORT takes in a USB-C port number and returns the I2C port number
 */
#define MUX_PORT_AND_ADDR(port, addr) ((port << 8) | (addr & 0xFF))
#define MUX_PORT(port) (usb_muxes[port].port_addr >> 8)
#define MUX_ADDR(port) (usb_muxes[port].port_addr & 0xFF)

/* Mux state attributes */
/* TODO: Directly use USB_PD_MUX_* everywhere and remove these 3 defines */
#define MUX_USB_ENABLED        USB_PD_MUX_USB_ENABLED
#define MUX_DP_ENABLED         USB_PD_MUX_DP_ENABLED
#define MUX_POLARITY_INVERTED  USB_PD_MUX_POLARITY_INVERTED
#define MUX_SAFE_MODE          USB_PD_MUX_SAFE_MODE

/* Mux modes, decoded to attributes */
enum typec_mux {
	TYPEC_MUX_NONE = 0,                /* Open switch */
	TYPEC_MUX_USB  = MUX_USB_ENABLED,  /* USB only */
	TYPEC_MUX_DP   = MUX_DP_ENABLED,   /* DP only */
	TYPEC_MUX_DOCK = MUX_USB_ENABLED | /* Both USB and DP */
			 MUX_DP_ENABLED,
	TYPEC_MUX_SAFE = MUX_SAFE_MODE,    /* Safe mode */
};

/* Mux driver function pointers */
struct usb_mux_driver {
	/**
	 * Initialize USB mux. This is called every time the MUX is access after
	 * being put in a fully disconnected state (low power mode).
	 *
	 * @param port usb port of mux (not port_addr)
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*init)(int port);

	/**
	 * Set USB mux state.
	 *
	 * @param port usb port of mux (not port_addr)
	 * @param mux_state State to set mux to.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*set)(int port, mux_state_t mux_state);

	/**
	 * Get current state of USB mux.
	 *
	 * @param port usb port of mux (not port_addr)
	 * @param mux_state Gets set to current state of mux.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*get)(int port, mux_state_t *mux_state);

	/**
	 * Optional method that is called after the mux fully disconnects.
	 *
	 * Note: this method does not need to be defined for TCPC/MUX combos
	 * where the TCPC is actively used since the PD state machine
	 * will put the chip into lower power mode.
	 *
	 * @param port usb port of mux (not port_addr)
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*enter_low_power_mode)(int port);
};

/* Flags used for usb_mux.flags */
#define USB_MUX_FLAG_NOT_TCPC BIT(0) /* TCPC/MUX device used only as MUX */

/* Describes a USB mux present in the system */
struct usb_mux {
	/*
	 * Used by driver. Muxes that are also the TCPC do not need to specify
	 * anything for this as they will use the values from tcpc_config_t. If
	 * this mux is also a TCPC but not used as the TCPC then use the
	 * MUX_PORT_AND_ADDR to pack the i2c port and i2c address into this
	 * field and use the USB_MUX_FLAG_NOT_TCPC flag.
	 */
	const int port_addr;

	/* Run-time flags with prefix USB_MUX_FLAG_ */
	const uint32_t flags;

	/* Mux driver */
	const struct usb_mux_driver *driver;

	/**
	 * Optional method for tuning for USB mux during mux->driver->init().
	 *
	 * @param port usb port of mux (not port_addr)
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*board_init)(int port);

	/*
	 * USB Type-C DP alt mode support. Notify Type-C controller
	 * there is DP dongle hot-plug.
	 * TODO: Move this function to usb_mux_driver struct.
	 */
	void (*hpd_update)(int port, int hpd_lvl, int hpd_irq);
};

/* Supported USB mux drivers */
extern const struct usb_mux_driver it5205_usb_mux_driver;
extern const struct usb_mux_driver pi3usb30532_usb_mux_driver;
extern const struct usb_mux_driver ps874x_usb_mux_driver;
extern const struct usb_mux_driver tcpm_usb_mux_driver;
extern const struct usb_mux_driver virtual_usb_mux_driver;

/* Supported hpd_update functions */
void virtual_hpd_update(int port, int hpd_lvl, int hpd_irq);

/* USB muxes present in system, ordered by PD port #, defined at board-level */
extern struct usb_mux usb_muxes[];

/*
 * Helper methods that either use tcpc communication or direct i2c
 * communication depending on how the TCPC/MUX device is configured.
 */
#ifdef CONFIG_USB_PD_TCPM_MUX
static inline int mux_write(int port, int reg, int val)
{
	return usb_muxes[port].flags & USB_MUX_FLAG_NOT_TCPC
		? i2c_write8(MUX_PORT(port), MUX_ADDR(port), reg, val)
		: tcpc_write(port, reg, val);
}

static inline int mux_read(int port, int reg, int *val)
{
	return usb_muxes[port].flags & USB_MUX_FLAG_NOT_TCPC
		? i2c_read8(MUX_PORT(port), MUX_ADDR(port), reg, val)
		: tcpc_read(port, reg, val);
}

static inline int mux_write16(int port, int reg, int val)
{
	return usb_muxes[port].flags & USB_MUX_FLAG_NOT_TCPC
		? i2c_write16(MUX_PORT(port), MUX_ADDR(port),
			      reg, val)
		: tcpc_write16(port, reg, val);
}

static inline int mux_read16(int port, int reg, int *val)
{
	return usb_muxes[port].flags & USB_MUX_FLAG_NOT_TCPC
		? i2c_read16(MUX_PORT(port), MUX_ADDR(port),
			     reg, val)
		: tcpc_read16(port, reg, val);
}
#endif /* CONFIG_USB_PD_TCPM_MUX */

/**
 * Initialize USB mux to its default state.
 *
 * @param port Port number.
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
