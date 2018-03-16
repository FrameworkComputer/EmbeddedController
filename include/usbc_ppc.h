/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USBC_PPC_H
#define __CROS_EC_USBC_PPC_H

#include "common.h"
#include "usb_pd_tcpm.h"

/* Common APIs for USB Type-C Power Path Controllers (PPC) */

struct ppc_drv {
	/**
	 * Initialize the PPC.
	 *
	 * @param port: The Type-C port number.
	 * @return EC_SUCCESS when init was successful, error otherwise.
	 */
	int (*init)(int port);

	/**
	 * Is the port sourcing Vbus?
	 *
	 * @param port: The Type-C port number.
	 * @return 1 if sourcing Vbus, 0 if not.
	 */
	int (*is_sourcing_vbus)(int port);

	/**
	 * Turn on/off the charge path FET, such that current flows into the
	 * system.
	 *
	 * @param port: The Type-C port number.
	 * @param enable: 1: Turn on the FET, 0: turn off the FET.
	 * @return EC_SUCCESS on success, error otherwise.
	 */
	int (*vbus_sink_enable)(int port, int enable);

	/**
	 * Turn on/off the source path FET, such that current flows from the
	 * system.
	 *
	 * @param port: The Type-C port number.
	 * @param enable: 1: Turn on the FET, 0: turn off the FET.
	 * @return EC_SUCCESS on success, error otherwise.
	 */
	int (*vbus_source_enable)(int port, int enable);

#ifdef CONFIG_USBC_PPC_POLARITY
	/**
	 * Inform the PPC of the polarity of the CC pins.
	 *
	 * @param port: The Type-C port number.
	 * @param polarity: 1: CC2 used for comms, 0: CC1 used for comms.
	 * @return EC_SUCCESS on success, error otherwise.
	 */
	int (*set_polarity)(int port, int polarity);
#endif

	/**
	 * Set the Vbus source path current limit
	 *
	 * @param port: The Type-C port number.
	 * @param rp: The Rp value which to approximately set the current limit.
	 * @return EC_SUCCESS on success, error otherwise.
	 */
	int (*set_vbus_source_current_limit)(int port, enum tcpc_rp_value rp);

	/**
	 * Discharge PD VBUS on src/sink disconnect & power role swap
	 *
	 * @param port: The Type-C port number.
	 * @param enable: 1 -> discharge vbus, 0 -> stop discharging vbus
	 * @return EC_SUCCESS on success, error otherwise.
	 */
	int (*discharge_vbus)(int port, int enable);

#ifdef CONFIG_USBC_PPC_VCONN
	/**
	 * Turn on/off the VCONN FET.
	 *
	 * @param port: The Type-C port number.
	 * @param enable: 1: enable VCONN FET 0: disable VCONN FET.
	 */
	int (*set_vconn)(int port, int enable);
#endif

#ifdef CONFIG_CMD_PPC_DUMP
	/**
	 * Perform a register dump of the PPC.
	 *
	 * @param port: The Type-C port number.
	 * @return EC_SUCCESS on success, error otherwise.
	 */
	int (*reg_dump)(int port);
#endif /* defined(CONFIG_CMD_PPC_DUMP) */

#ifdef CONFIG_USB_PD_VBUS_DETECT_PPC
	/**
	 * Determine if VBUS is present or not.
	 *
	 * @param port: The Type-C port number.
	 * @return 1 if VBUS is present, 0 if not.
	 */
	int (*is_vbus_present)(int port);
#endif /* defined(CONFIG_USB_PD_VBUS_DETECT_PPC) */
};


/* PPC SNK/SRC switch control driven by EC GPIO */
#define PPC_CFG_FLAGS_GPIO_CONTROL (1 << 0)

struct ppc_config_t {
	/* Used for PPC_CFG_FLAGS_* defined above */
	uint32_t flags;
	int i2c_port;
	int i2c_addr;
	/* snk|src_gpio only required if PPC_CFG_FLAGS_GPIO_CONTROL is set */
	enum gpio_signal snk_gpio;
	enum gpio_signal src_gpio;
	const struct ppc_drv *drv;
};

extern const struct ppc_config_t ppc_chips[];
extern const unsigned int ppc_cnt;

/**
 * Initializes the PPC for the specified port.
 *
 * @param port: The Type-C port number.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_init(int port);

/**
 * Determine if VBUS is present or not.
 *
 * @param port: The Type-C port number.
 * @return 1 if VBUS is present, 0 if not.
 */
int ppc_is_vbus_present(int port);

/**
 * Is the port sourcing Vbus?
 *
 * @param port: The Type-C port number.
 * @return 1 if sourcing Vbus, 0 if not.
 */
int ppc_is_sourcing_vbus(int port);

/**
 * Inform the PPC of the polarity of the CC pins.
 *
 * @param port: The Type-C port number.
 * @param polarity: 1: CC2 used for comms, 0: CC1 used for comms.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_set_polarity(int port, int polarity);

/**
 * Set the Vbus source path current limit
 *
 * @param port: The Type-C port number.
 * @param rp: The Rp value which to approximately set the current limit.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_set_vbus_source_current_limit(int port, enum tcpc_rp_value rp);

/**
 * Turn on/off the VCONN FET.
 *
 * @param port: The Type-C port number.
 * @param enable: 1: enable VCONN FET 0: disable VCONN FET.
 */
int ppc_set_vconn(int port, int enable);

/**
 * Discharge PD VBUS on src/sink disconnect & power role swap
 *
 * @param port: The Type-C port number.
 * @param enable: 1 -> discharge vbus, 0 -> stop discharging vbus
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_discharge_vbus(int port, int enable);

/**
 * Turn on/off the charge path FET, such that current flows into the
 * system.
 *
 * @param port: The Type-C port number.
 * @param enable: 1: Turn on the FET, 0: turn off the FET.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_vbus_sink_enable(int port, int enable);

/**
 * Turn on/off the source path FET, such that current flows from the
 * system.
 *
 * @param port: The Type-C port number.
 * @param enable: 1: Turn on the FET, 0: turn off the FET.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_vbus_source_enable(int port, int enable);

/**
 * Board specific callback when a port overcurrents.
 *
 * @param port: The Type-C port which overcurrented.
 */
void board_overcurrent_event(int port);

#endif /* !defined(__CROS_EC_USBC_PPC_H) */
