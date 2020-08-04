/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB mux driver */

#ifndef __CROS_EC_USB_MUX_H
#define __CROS_EC_USB_MUX_H

#include "ec_commands.h"
#include "i2c.h"
#include "tcpm.h"
#include "usb_charge.h"
#include "usb_pd.h"

/* Flags used for usb_mux.flags */
#define USB_MUX_FLAG_NOT_TCPC BIT(0) /* TCPC/MUX device used only as MUX */
#define USB_MUX_FLAG_SET_WITHOUT_FLIP BIT(1) /* SET should not flip */

/*
 * USB-C mux state
 *
 * A bitwise combination of the USB_PD_MUX_* flags.
 * Note: this is 8 bits right now to make ec_response_usb_pd_mux_info size.
 */
typedef uint8_t mux_state_t;

/* Mux driver function pointers */
struct usb_mux;
struct usb_mux_driver {
	/**
	 * Initialize USB mux. This is called every time the MUX is
	 * access after being put in a fully disconnected state (low
	 * power mode).
	 *
	 * @param me usb_mux
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*init)(const struct usb_mux *me);

	/**
	 * Set USB mux state.
	 *
	 * @param me usb_mux
	 * @param mux_state State to set mux to.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*set)(const struct usb_mux *me, mux_state_t mux_state);

	/**
	 * Get current state of USB mux.
	 *
	 * @param me usb_mux
	 * @param mux_state Gets set to current state of mux.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*get)(const struct usb_mux *me, mux_state_t *mux_state);

	/**
	 * Optional method that is called after the mux fully disconnects.
	 *
	 * Note: this method does not need to be defined for TCPC/MUX combos
	 * where the TCPC is actively used since the PD state machine
	 * will put the chip into lower power mode.
	 *
	 * @param me usb_mux
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*enter_low_power_mode)(const struct usb_mux *me);

	/**
	 * Optional method that is called on HOOK_CHIPSET_RESET.
	 *
	 * @param me usb_mux
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*chipset_reset)(const struct usb_mux *me);
};

/* Describes a USB mux present in the system */
struct usb_mux {
	/*
	 * This is index into usb_muxes that points to the start of the
	 * possible chain of usb_mux entries that this entry is on.
	 */
	int usb_port;

	/*
	 * I2C port and address. This is optional if your MUX is not
	 * an I2C interface.  If this is the case, use usb_port to
	 * index an exernal array to track your connection parameters,
	 * if they are needed.  One case of this would be a driver
	 * that will use usb_port as an index into tcpc_config_t to
	 * gather the necessary information to communicate with the MUX
	 */
	uint16_t i2c_port;
	uint16_t i2c_addr_flags;

	/* Run-time flags with prefix USB_MUX_FLAG_ */
	uint32_t flags;

	/* Mux driver */
	const struct usb_mux_driver *driver;

	/* Linked list chain of secondary MUXes. NULL terminated */
	const struct usb_mux *next_mux;

	/**
	 * Optional method for tuning for USB mux during mux->driver->init().
	 *
	 * @param me usb_mux
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*board_init)(const struct usb_mux *me);

	/*
	 * USB mux/retimer board specific set mux_state.
	 *
	 * @param me usb_mux
	 * @param mux_state State to set mode to.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*board_set)(const struct usb_mux *me, mux_state_t mux_state);

	/*
	 * TODO: Consider moving this to usb_mux_driver struct
	 *
	 * USB Type-C DP alt mode support. Notify Type-C controller
	 * there is DP dongle hot-plug.
	 *
	 * @param me usb_mux
	 * @param hpd_lvl Level
	 * @param hpd_irq IRQ
	 */
	void (*hpd_update)(const struct usb_mux *me,
			   int hpd_lvl, int hpd_irq);
};

/* Supported USB mux drivers */
extern const struct usb_mux_driver amd_fp5_usb_mux_driver;
extern const struct usb_mux_driver anx7440_usb_mux_driver;
extern const struct usb_mux_driver it5205_usb_mux_driver;
extern const struct usb_mux_driver pi3usb3x532_usb_mux_driver;
extern const struct usb_mux_driver ps8740_usb_mux_driver;
extern const struct usb_mux_driver ps8743_usb_mux_driver;
extern const struct usb_mux_driver tcpm_usb_mux_driver;
extern const struct usb_mux_driver virtual_usb_mux_driver;

/* USB muxes present in system, ordered by PD port #, defined at board-level */
#ifdef CONFIG_USB_MUX_RUNTIME_CONFIG
extern struct usb_mux usb_muxes[];
#else
extern const struct usb_mux usb_muxes[];
#endif

/* Supported hpd_update functions */
void virtual_hpd_update(const struct usb_mux *me, int hpd_lvl, int hpd_irq);

/*
 * Helper methods that either use tcpc communication or direct i2c
 * communication depending on how the TCPC/MUX device is configured.
 */
#ifdef CONFIG_USB_PD_TCPM_MUX
static inline int mux_write(const struct usb_mux *me, int reg, int val)
{
	return me->flags & USB_MUX_FLAG_NOT_TCPC
		? i2c_write8(me->i2c_port, me->i2c_addr_flags, reg, val)
		: tcpc_write(me->usb_port, reg, val);
}

static inline int mux_read(const struct usb_mux *me, int reg, int *val)
{
	return me->flags & USB_MUX_FLAG_NOT_TCPC
		? i2c_read8(me->i2c_port, me->i2c_addr_flags, reg, val)
		: tcpc_read(me->usb_port, reg, val);
}

static inline int mux_write16(const struct usb_mux *me, int reg, int val)
{
	return me->flags & USB_MUX_FLAG_NOT_TCPC
		? i2c_write16(me->i2c_port, me->i2c_addr_flags, reg, val)
		: tcpc_write16(me->usb_port, reg, val);
}

static inline int mux_read16(const struct usb_mux *me, int reg, int *val)
{
	return me->flags & USB_MUX_FLAG_NOT_TCPC
		? i2c_read16(me->i2c_port, me->i2c_addr_flags, reg, val)
		: tcpc_read16(me->usb_port, reg, val);
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
