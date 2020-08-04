/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USBC_PPC_H
#define __CROS_EC_USBC_PPC_H

#include "common.h"
#include "usb_pd_tcpm.h"

/* Common APIs for USB Type-C Power Path Controllers (PPC) */

/*
 * Number of times a port may overcurrent before we latch off the port until a
 * physical disconnect is detected.
 */
#define PPC_OC_CNT_THRESH 3

/*
 * Number of seconds until a latched-off port is re-enabled for sourcing after
 * detecting a physical disconnect.
 */
#define PPC_OC_COOLDOWN_DELAY_US (2 * SECOND)

/*
 * NOTE: The pointers to functions in the ppc_drv structure can now be NULL
 * which will indicate and return NOT_IMPLEMENTED from the main calling
 * function
 */
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

#ifdef CONFIG_USBC_PPC_SBU
	/**
	 * Turn on/off the SBU FETs.
	 *
	 * @param port: The Type-C port number.
	 * @param enable: 1: enable SBU FETs 0: disable SBU FETs.
	 */
	int (*set_sbu)(int port, int enable);
#endif /* CONFIG_USBC_PPC_SBU */

#ifdef CONFIG_USBC_PPC_VCONN
	/**
	 * Turn on/off the VCONN FET.
	 *
	 * @param port: The Type-C port number.
	 * @param enable: 1: enable VCONN FET 0: disable VCONN FET.
	 */
	int (*set_vconn)(int port, int enable);
#endif

#ifdef CONFIG_USB_PD_FRS_PPC
	/**
	 * Turn on/off the FRS trigger
	 *
	 * @param port: The Type-C port number.
	 * @return EC_SUCCESS on success, error otherwise
	 */
	int (*set_frs_enable)(int port, int enable);
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

	/**
	 * Optional method to put the PPC into its lowest power state. In this
	 * state it should still fire interrupts if Vbus changes etc.
	 *
	 * @param port: The Type-C port number.
	 * @return EC_SUCCESS on success, error otherwise.
	 */
	int (*enter_low_power_mode)(int port);
};

struct ppc_config_t {
	int i2c_port;
	uint16_t i2c_addr_flags;
	const struct ppc_drv *drv;
	int frs_en;
};

extern struct ppc_config_t ppc_chips[];
extern unsigned int ppc_cnt;

/**
 * Common CPRINTS implementation so that PPC driver messages are consistent.
 *
 * @param string: message string to display on the console.
 * @param port: The Type-C port number
 */
int ppc_prints(const char *string, int port);

/**
 * Common CPRINTS for PPC drivers with an error code.
 *
 * @param string: message string to display on the console.
 * @param port: The Type-C port number
 * @param error: The error code to display at the end of the message.
 */
int ppc_err_prints(const char *string, int port, int error);

/**
 * Increment the overcurrent event counter.
 *
 * @param port: The Type-C port that has overcurrented.
 * @return EC_SUCCESS on success, EC_ERROR_INVAL if non-existent port.
 */
int ppc_add_oc_event(int port);

/**
 * Clear the overcurrent event counter.
 *
 * @param port: The Type-C port's counter to clear.
 * @return EC_SUCCESS on success, EC_ERROR_INVAL if non-existent port.
 */
int ppc_clear_oc_event_counter(int port);

/**
 * Discharge PD VBUS on src/sink disconnect & power role swap
 *
 * @param port: The Type-C port number.
 * @param enable: 1 -> discharge vbus, 0 -> stop discharging vbus
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_discharge_vbus(int port, int enable);

/**
 * Initializes the PPC for the specified port.
 *
 * @param port: The Type-C port number.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_init(int port);

/**
 * Is the port latched off due to multiple overcurrent events in succession?
 *
 * @param port: The Type-C port number.
 * @return 1 if the port is latched off, 0 if it is not latched off.
 */
int ppc_is_port_latched_off(int port);

/**
 * Is the port sourcing Vbus?
 *
 * @param port: The Type-C port number.
 * @return 1 if sourcing Vbus, 0 if not.
 */
int ppc_is_sourcing_vbus(int port);

/**
 * Determine if VBUS is present or not.
 *
 * @param port: The Type-C port number.
 * @return 1 if VBUS is present, 0 if not.
 */
int ppc_is_vbus_present(int port);

/**
 * Inform the PPC module that a sink is connected.
 *
 * This is used such that it can determine when to clear the overcurrent events
 * counter for a port.
 * @param port: The Type-C port number.
 * @param is_connected: 1: if sink is connected on this port, 0: if not
 *                      connected.
 */
void ppc_sink_is_connected(int port, int is_connected);

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
 * Turn on/off the SBU FETs.
 *
 * @param port: The Type-C port number.
 * @param enable: 1: enable SBU FETs 0: disable SBU FETs.
 */
int ppc_set_sbu(int port, int enable);

/**
 * Turn on/off the VCONN FET.
 *
 * @param port: The Type-C port number.
 * @param enable: 1: enable VCONN FET 0: disable VCONN FET.
 */
int ppc_set_vconn(int port, int enable);

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
 * @param is_overcurrented: 1 if port overcurrented, 0 if the condition is gone.
 */
void board_overcurrent_event(int port, int is_overcurrented);

/**
 * Put the PPC into its lowest power state. In this state it should still fire
 * interrupts if Vbus changes etc. This is called by board-specific code when
 * appropriate.
 *
 * @param port: The Type-C port number.
 * @return EC_SUCCESS on success, error otherwise.
 */
int ppc_enter_low_power_mode(int port);

/**
 * Board specific callback to check if the PPC interrupt is still asserted
 *
 * @param port: The Type-C port number to check
 * @return 0 if interrupt is cleared, 1 if it is still on
 */
int ppc_get_alert_status(int port);

/**
 * Turn on/off the FRS trigger
 *
 * @param port: The Type-C port number.
 * @return EC_SUCCESS on success, error otherwise
 */
int ppc_set_frs_enable(int port, int enable);

#endif /* !defined(__CROS_EC_USBC_PPC_H) */
