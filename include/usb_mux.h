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
 * Note: this is 8 bits right now to make ec_response_usb_pd_mux_info size.
 */
typedef uint8_t mux_state_t;

/*
 * Packing and Unpacking defines used with USB_MUX_FLAG_NOT_TCPC
 * MUX_PORT takes in a USB-C port number and returns the I2C port number
 */
#define MUX_PORT_AND_ADDR(port, addr) ((port << 8) | (addr & 0xFF))
#define MUX_PORT(port) (usb_muxes[port].port_addr >> 8)
#define MUX_ADDR(port) (usb_muxes[port].port_addr & 0xFF)

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
extern const struct usb_mux_driver amd_fp5_usb_mux_driver;
extern const struct usb_mux_driver anx7440_usb_mux_driver;
extern const struct usb_mux_driver it5205_usb_mux_driver;
extern const struct usb_mux_driver pi3usb3x532_usb_mux_driver;
extern const struct usb_mux_driver ps874x_usb_mux_driver;
extern const struct usb_mux_driver tcpm_usb_mux_driver;
extern const struct usb_mux_driver virtual_usb_mux_driver;

/* Supported hpd_update functions */
void virtual_hpd_update(int port, int hpd_lvl, int hpd_irq);

/* USB muxes present in system, ordered by PD port #, defined at board-level */
extern struct usb_mux usb_muxes[];

/*
 * Retimer driver function pointers
 *
 * The retimer driver is driven by calls to the MUX API.  These are not
 * called directly anywhere else in the code.
 */
struct usb_retimer_driver {
	/**
	 * Initialize USB retimer. This is called every time the MUX is
	 * access after being put in a fully disconnected state (low power
	 * mode).
	 *
	 * @param port usb port of redriver (not port_addr)
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*init)(int port);

	/**
	 * Put USB retimer in low power mode. This is called when the MUX
	 * is put into low power mode).
	 *
	 * @param port usb port of redriver (not port_addr)
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*enter_low_power_mode)(int port);

	/**
	 * Set USB retimer state.
	 *
	 * @param port usb port of retimer (not port_addr)
	 * @param mux_state State to set retimer mode to.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*set)(int port, mux_state_t mux_state);
};

/* Describes a USB retimer present in the system */
struct usb_retimer {
	/*
	 * All of the fields are provided on an as needed basis.
	 * If your retimer does not use the provided machanism then
	 * values would not be set (defaulted to 0/NULL).  This
	 * defaulting includes the driver field, which would indicate
	 * no retimer driver is to be called.
	 */

	/* I2C port and slave address */
	const int i2c_port;
	uint16_t i2c_addr_flags;

	/* Driver interfaces for this retimer */
	const struct usb_retimer_driver *driver;

	/*
	 * USB retimer board specific tune on set mux_state.
	 *
	 * @param port usb port of retimer (not port_addr)
	 * @param mux_state State to set retimer mode to.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*tune)(int port, mux_state_t mux_state);
};

/*
 * USB retimers present in system, ordered by PD port #, defined at
 * board-level
 */
extern struct usb_retimer usb_retimers[];

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
void usb_mux_set(int port, mux_state_t mux_mode,
		 enum usb_switch usb_config, int polarity);

/**
 * Query superspeed mux status on type-C port.
 *
 * @param port port number.
 * @return current MUX state (USB_PD_MUX_*).
 */
mux_state_t usb_mux_get(int port);

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

/**
 * Update the hot-plug event.
 *
 * @param port port number.
 * @param hpd_lvl HPD level.
 * @param hpd_irq HPD IRQ.
 */
void usb_mux_hpd_update(int port, int hpd_lvl, int hpd_irq);

#endif
