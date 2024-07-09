/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB mux driver */

#ifndef __CROS_EC_USB_MUX_H
#define __CROS_EC_USB_MUX_H

#include "ec_commands.h"
#include "i2c.h"
#include "tcpm/tcpm.h"
#include "usb_charge.h"
#include "usb_pd.h"

/*
 * If compiling with Zephyr, include the USB_MUX_FLAG_ definitions that are
 * shared with device tree
 */
#ifdef CONFIG_ZEPHYR

#include "dt-bindings/usbc_mux.h"

#else /* !CONFIG_ZEPHYR */

#ifdef __cplusplus
extern "C" {
#endif

/* Flags used for usb_mux.flags */
#define USB_MUX_FLAG_NOT_TCPC BIT(0) /* TCPC/MUX device used only as MUX */
#define USB_MUX_FLAG_SET_WITHOUT_FLIP BIT(1) /* SET should not flip */
#define USB_MUX_FLAG_RESETS_IN_G3 BIT(2) /* Mux chip will reset in G3 */
#define USB_MUX_FLAG_POLARITY_INVERTED BIT(3) /* Mux polarity is inverted */
#define USB_MUX_FLAG_CAN_IDLE BIT(4) /* MUX supports idle mode */

#endif /* CONFIG_ZEPHYR */

/* usb_mux.hpd_update API only specifies 2 relevant bits in mux_state */
#define MUX_STATE_HPD_UPDATE_MASK (USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ)

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
	 * @param[in]  me usb_mux
	 * @param[in]  mux_state State to set mux to.
	 * @param[out] bool ack_required - indication of whether this mux needs
	 * to wait on a host command ACK at the end of a set
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*set)(const struct usb_mux *me, mux_state_t mux_state,
		   bool *ack_required);

	/**
	 * Get current state of USB mux.
	 *
	 * @param me usb_mux
	 * @param mux_state Gets set to current state of mux.
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*get)(const struct usb_mux *me, mux_state_t *mux_state);

	/**
	 * Return if retimer supports firmware update
	 *
	 * @return true  - supported
	 *         false - not supported
	 */
	bool (*is_retimer_fw_update_capable)(void);

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

	/**
	 * Optional method that is called on HOOK_CHIPSET_{SUSPEND,RESUME}.
	 *
	 * Note: This notifies the mux that the rest of the system
	 * entered (left) a low power state such as S0ix or S3. This
	 * enables the mux driver to make power optimization decisions
	 * such as powering down the USB3 retimer when not in use. If
	 * the associated port is in low power mode, idle mode is not
	 * used.
	 *
	 * @param me usb_mux
	 * @param idle bool
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*set_idle_mode)(const struct usb_mux *me, bool idle);

#ifdef CONFIG_CMD_RETIMER
	/**
	 * Console command to read the retimer registers
	 *
	 * @param me usb_mux
	 * @param offset Register offset
	 * @param data Data to be read
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*retimer_read)(const struct usb_mux *me, const uint32_t offset,
			    uint32_t *data);

	/**
	 * Console command to write to the retimer registers
	 *
	 * @param me usb_mux
	 * @param offset Register offset
	 * @param data Data to be written
	 * @return EC_SUCCESS on success, non-zero error code on failure.
	 */
	int (*retimer_write)(const struct usb_mux *me, const uint32_t offset,
			     uint32_t data);
#endif /* CONFIG_CMD_RETIMER */
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
	 * USB Type-C DP alt mode support. Notify Type-C controller
	 * there is DP dongle hot-plug.
	 *
	 * @param[in]  me usb_mux
	 * @param[in]  mux_state with HPD IRQ and HPD LVL flags set
	 *	       accordingly. Other flags are undefined.
	 * @param[out] ack_required: indication of whether this function
	 *	       requires a wait for an AP ACK after
	 */
	void (*hpd_update)(const struct usb_mux *me, mux_state_t hpd_state,
			   bool *ack_required);
};

/* Linked list chain of secondary MUXes. NULL terminated */
struct usb_mux_chain {
	/* Structure describing USB mux */
	const struct usb_mux *mux;

	/* Pointer to next mux */
	const struct usb_mux_chain *next;
};

/* Supported USB mux drivers */
extern const struct usb_mux_driver amd_fp5_usb_mux_driver;
extern const struct usb_mux_driver amd_fp6_usb_mux_driver;
extern const struct usb_mux_driver amd_fp8_usb_mux_driver;
extern const struct usb_mux_driver anx7440_usb_mux_driver;
extern const struct usb_mux_driver it5205_usb_mux_driver;
extern const struct usb_mux_driver pi3usb3x532_usb_mux_driver;
extern const struct usb_mux_driver ps8740_usb_mux_driver;
extern const struct usb_mux_driver ps8743_usb_mux_driver;
extern const struct usb_mux_driver ps8822_usb_mux_driver;
extern const struct usb_mux_driver tcpm_usb_mux_driver;
extern const struct usb_mux_driver tusb1064_usb_mux_driver;
extern const struct usb_mux_driver virtual_usb_mux_driver;

/* USB muxes present in system, ordered by PD port #, defined at board-level */
#ifdef CONFIG_USB_MUX_RUNTIME_CONFIG
extern struct usb_mux_chain usb_muxes[];
#else
extern const struct usb_mux_chain usb_muxes[];
#endif

/* Supported hpd_update functions */
void virtual_hpd_update(const struct usb_mux *me, mux_state_t hpd_state,
			bool *ack_required);

/*
 * Helper methods that either use tcpc communication or direct i2c
 * communication depending on how the TCPC/MUX device is configured.
 */
#ifdef CONFIG_USB_PD_TCPM_MUX
static inline int mux_write(const struct usb_mux *me, int reg, int val)
{
	return me->flags & USB_MUX_FLAG_NOT_TCPC ?
		       i2c_write8(me->i2c_port, me->i2c_addr_flags, reg, val) :
		       tcpc_write(me->usb_port, reg, val);
}

static inline int mux_read(const struct usb_mux *me, int reg, int *val)
{
	return me->flags & USB_MUX_FLAG_NOT_TCPC ?
		       i2c_read8(me->i2c_port, me->i2c_addr_flags, reg, val) :
		       tcpc_read(me->usb_port, reg, val);
}

static inline int mux_write16(const struct usb_mux *me, int reg, int val)
{
	return me->flags & USB_MUX_FLAG_NOT_TCPC ?
		       i2c_write16(me->i2c_port, me->i2c_addr_flags, reg, val) :
		       tcpc_write16(me->usb_port, reg, val);
}

static inline int mux_read16(const struct usb_mux *me, int reg, int *val)
{
	return me->flags & USB_MUX_FLAG_NOT_TCPC ?
		       i2c_read16(me->i2c_port, me->i2c_addr_flags, reg, val) :
		       tcpc_read16(me->usb_port, reg, val);
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
#if defined(CONFIG_USBC_SS_MUX) || defined(CONFIG_ZTEST)
void usb_mux_set(int port, mux_state_t mux_mode, enum usb_switch usb_config,
		 int polarity);
#else
static inline void usb_mux_set(int port, mux_state_t mux_mode,
			       enum usb_switch usb_config, int polarity)
{
}
#endif

/**
 * Mark that mux ACK has been received for this port's pending set
 *
 * @param port port number.
 */
void usb_mux_set_ack_complete(int port);

/**
 * Configure superspeed muxes on type-C port for only one index in the mux
 * chain
 *
 * @param port port number.
 * @param index index of mux or retimer to set
 * @param mux_mode mux selected function.
 * @param usb_config usb2.0 selected function.
 * @param polarity plug polarity (0=CC1, 1=CC2).
 */
#if defined(CONFIG_USBC_SS_MUX) || defined(CONFIG_ZTEST)
void usb_mux_set_single(int port, int index, mux_state_t mux_mode,
			enum usb_switch usb_mode, int polarity);
#else
static inline void usb_mux_set_single(int port, int index, mux_state_t mux_mode,
				      enum usb_switch usb_mode, int polarity)
{
}
#endif
/**
 * Query superspeed mux status on type-C port.
 *
 * @param port port number.
 * @return current MUX state (USB_PD_MUX_*).
 */
#if defined(CONFIG_USBC_SS_MUX) || defined(CONFIG_ZTEST)
mux_state_t usb_mux_get(int port);
#else
static inline mux_state_t usb_mux_get(int port)
{
	return 0;
}
#endif

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
 * @param mux_state HPD IRQ and LVL mux flags
 */
void usb_mux_hpd_update(int port, mux_state_t hpd_state);

/**
 * Port information about retimer firmware update support.
 *
 * @return which ports support retimer firmware update
 *         Bits[7:0]: represent PD ports 0-7;
 *         each bit
 *         = 1, this port supports retimer firmware update;
 *         = 0, not support.
 */
int usb_mux_retimer_fw_update_port_info(void);

/**
 * Check whether this port has pending mux sets
 *
 * @param  port USB-C port number
 * @return True if all pending mux sets have completed
 */
bool usb_mux_set_completed(int port);

#ifdef __cplusplus
}
#endif

#endif
