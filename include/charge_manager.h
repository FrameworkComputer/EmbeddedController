/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#ifndef __CROS_EC_CHARGE_MANAGER_H
#define __CROS_EC_CHARGE_MANAGER_H

#include "common.h"
#include "ec_commands.h"

/* Charge port that indicates no active port */
#define CHARGE_PORT_NONE -1
#define CHARGE_CEIL_NONE -1

/* Initial charge state */
#define CHARGE_CURRENT_UNINITIALIZED -1
#define CHARGE_VOLTAGE_UNINITIALIZED -1

/* Only track BC1.2 charge current if we support BC1.2 charging */
#if defined(HAS_TASK_USB_CHG) || defined(HAS_TASK_USB_CHG_P0) || \
	defined(CONFIG_PLATFORM_EC_USB_CHARGER_SINGLE_TASK) ||   \
	defined(TEST_BUILD)
#define CHARGE_MANAGER_BC12
#endif

/**
 * Time to delay for detecting the charger type (must be long enough for BC1.2
 * driver to get supplier information and notify charge manager).
 */
#define CHARGE_DETECT_DELAY (2 * SECOND)

/*
 * Commonly-used charge suppliers listed in no particular order.
 * Don't forget to update CHARGE_SUPPLIER_NAME and supplier_priority.
 */
enum charge_supplier {
	CHARGE_SUPPLIER_NONE = -1,
	CHARGE_SUPPLIER_PD,
	CHARGE_SUPPLIER_TYPEC,
	CHARGE_SUPPLIER_TYPEC_DTS,
#ifdef CHARGE_MANAGER_BC12
	CHARGE_SUPPLIER_BC12_DCP,
	CHARGE_SUPPLIER_BC12_CDP,
	CHARGE_SUPPLIER_BC12_SDP,
	CHARGE_SUPPLIER_PROPRIETARY,
	CHARGE_SUPPLIER_TYPEC_UNDER_1_5A,
	CHARGE_SUPPLIER_OTHER,
	CHARGE_SUPPLIER_VBUS,
#endif /* CHARGE_MANAGER_BC12 */
#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
	CHARGE_SUPPLIER_DEDICATED,
#endif
	CHARGE_SUPPLIER_COUNT
};

#ifdef CHARGE_MANAGER_BC12
#define CHARGE_SUPPLIER_NAME_BC12                          \
	[CHARGE_SUPPLIER_BC12_DCP] = "BC12_DCP",           \
	[CHARGE_SUPPLIER_BC12_CDP] = "BC12_CDP",           \
	[CHARGE_SUPPLIER_BC12_SDP] = "BC12_SDP",           \
	[CHARGE_SUPPLIER_PROPRIETARY] = "BC12_PROP",       \
	[CHARGE_SUPPLIER_TYPEC_UNDER_1_5A] = "USBC_U1_5A", \
	[CHARGE_SUPPLIER_OTHER] = "BC12_OTHER", [CHARGE_SUPPLIER_VBUS] = "VBUS",
#else
#define CHARGE_SUPPLIER_NAME_BC12
#endif
#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
#define CHARGE_SUPPLIER_NAME_DEDICATED \
	[CHARGE_SUPPLIER_DEDICATED] = "DEDICATED",
#else
#define CHARGE_SUPPLIER_NAME_DEDICATED
#endif
#define CHARGE_SUPPLIER_NAME_QI

#define CHARGE_SUPPLIER_NAME                                           \
	[CHARGE_SUPPLIER_PD] = "PD", [CHARGE_SUPPLIER_TYPEC] = "USBC", \
	[CHARGE_SUPPLIER_TYPEC_DTS] = "USBC_DTS",                      \
	CHARGE_SUPPLIER_NAME_BC12 CHARGE_SUPPLIER_NAME_DEDICATED       \
		CHARGE_SUPPLIER_NAME_QI

/*
 * Charge supplier priority: lower number indicates higher priority.
 * Default priority is in charge_manager.c. It can be overridden by boards.
 */
extern const int supplier_priority[];

/* Charge tasks report available current and voltage */
struct charge_port_info {
	int current;
	int voltage;
};

/**
 * Called by charging tasks to update their available charge.
 *
 * @param supplier	Charge supplier to update.
 * @param port		Charge port to update.
 * @param charge	Charge port current / voltage. If NULL, current = 0
 * 			voltage = 0 will be used.
 */
void charge_manager_update_charge(int supplier, int port,
				  const struct charge_port_info *charge);

/* Partner port dualrole capabilities */
enum dualrole_capabilities {
	CAP_UNKNOWN,
	CAP_DUALROLE,
	CAP_DEDICATED,
};

/**
 * Notify charge_manager of a partner dualrole capability change.
 *
 * @param port			Charge port which changed.
 * @param cap			New port capability.
 */
void charge_manager_update_dualrole(int port, enum dualrole_capabilities cap);

/**
 * Tell charge_manager to leave safe mode and switch to standard port / ILIM
 * selection logic.
 */
void charge_manager_leave_safe_mode(void);

/**
 * Charge ceiling can be set independently by different tasks / functions,
 * for different purposes.
 */
enum ceil_requestor {
	/* Set by PD task, during negotiation */
	CEIL_REQUESTOR_PD,
	/* Set by host commands */
	CEIL_REQUESTOR_HOST,
	/* Number of ceiling groups */
	CEIL_REQUESTOR_COUNT,
};

#define CHARGE_PORT_COUNT \
	(CONFIG_USB_PD_PORT_MAX_COUNT + CONFIG_DEDICATED_CHARGE_PORT_COUNT)
#if (CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0)

/**
 * By default, dedicated port has following properties:
 *
 * - dedicated port is sink only.
 * - dedicated port is always connected.
 * - dedicated port is given highest priority (supplier type is always
 *   CHARGE_SUPPLIER_DEDICATED).
 * - dualrole capability of dedicated port is always CAP_DEDICATED.
 * - there's only one dedicated port, its number is larger than PD port number.
 *
 * Sink property can be customized by implementing board_charge_port_is_sink()
 * and board_fill_source_power_info().
 * Connected can be customized by implementing board_charge_port_is_connected().
 */
#if !defined(DEDICATED_CHARGE_PORT)
#error "DEDICATED_CHARGE_PORT must be defined"
#elif DEDICATED_CHARGE_PORT < CONFIG_USB_PD_PORT_MAX_COUNT
#error "DEDICATED_CHARGE_PORT must larger than pd port numbers"
#endif /* !defined(DEDICATED_CHARGE_PORT) */

#endif /* CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0 */

/**
 * Update charge ceiling for a given port. The ceiling can be set independently
 * for several requestors, and the min. ceil will be enforced.
 *
 * @param port			Charge port to update.
 * @param requestor		Charge ceiling requestor.
 * @param ceil			Charge ceiling (mA).
 */
void charge_manager_set_ceil(int port, enum ceil_requestor requestor, int ceil);

/*
 * Update PD charge ceiling for a given port. In the event that our ceiling
 * is currently above ceil, change the current limit before returning, without
 * waiting for a charge manager refresh. This function should only be used in
 * time-critical situations where we absolutely cannot proceed without limiting
 * our input current, and it should only be called from the PD tasks.
 * If you ever call this function then you are a terrible person.
 */
void charge_manager_force_ceil(int port, int ceil);

/**
 * Select an 'override port', a port which is always the preferred charge port.
 *
 * @param port			Charge port to select as override, or
 *				OVERRIDE_OFF to select no override port,
 *				or OVERRIDE_DONT_CHARGE to specific that no
 *				charge port should be selected.
 * @return			EC_SUCCESS on success,
 *				the other ec_error_list status on failure.
 */
int charge_manager_set_override(int port);

/**
 * Get the override port.
 *
 * @return	Port number or OVERRIDE_OFF or OVERRIDE_DONT_CHARGE.
 */
int charge_manager_get_override(void);

/**
 * Get the current active charge port, as determined by charge manager.
 *
 * @return	Current active charge port.
 */
int charge_manager_get_active_charge_port(void);

/**
 * Get the current selected charge port, as determined by charge manager.
 * This is the charge port that is either active or that we may be
 * transitioning to because a better choice has been given as an option
 * but that transition has not completed.
 *
 * @return	Current selected charge port.
 */
int charge_manager_get_selected_charge_port(void);

/**
 * Get the power limit set by charge manager.
 *
 * @return	Power limit (uW).
 */
int charge_manager_get_power_limit_uw(void);

/**
 * Get the charger current (mA) value.
 *
 * @return	Charger current (mA) or CHARGE_CURRENT_UNINITIALIZED.
 */
int charge_manager_get_charger_current(void);

/**
 * Get the charger voltage (mV) value.
 *
 * @return	Charger voltage (mV) or CHARGE_VOLTAGE_UNINITIALIZED.
 */
int charge_manager_get_charger_voltage(void);

/**
 * Get the charger supplier.
 *
 * @return	enum charge_supplier
 */
enum charge_supplier charge_manager_get_supplier(void);

/**
 * Get the current VBUS voltage.
 *
 * @param port The USB-C port to query
 * @return The current VBUS voltage in mV or 0 if it could not be determined
 */
int charge_manager_get_vbus_voltage(int port);

/**
 * Get the current limit of CHARGE_PD_SUPPLIER.
 *
 * @return	The CHARGE_SUPPLIER_PD current limit in mA or
 *		CHARGE_CURRENT_UNINITIALIZED if the supplier is not
 *		CHARGE_SUPPLIER_PD.
 */
int charge_manager_get_pd_current_uncapped(void);

#ifdef CONFIG_USB_PD_LOGGING
/* Save power state log entry for the given port */
void charge_manager_save_log(int port);
#endif

/**
 * Update whether a given port is sourcing current.
 *
 * @param port		Port number to be updated.
 * @param enable	0 if the source port is disabled;
 *			Otherwise the source port is enabled.
 */
void charge_manager_source_port(int port, int enable);

/**
 * Get PD source power data objects.
 *
 * @param src_pdo	Pointer to the data to return.
 * @param port		Current port to evaluate against.
 * @return number of PDOs returned.
 */
int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port);

/* Board-level callback functions */

/**
 * Set the passed charge port as active.`
 *
 * @param charge_port	Charge port to be enabled.
 * @return		EC_SUCCESS if the charge port is accepted,
 *			other ec_error_list status otherwise.
 */
int board_set_active_charge_port(int charge_port);

/**
 * Set the charge current limit.
 *
 * The default implementation of this function derates charge_ma by
 * CONFIG_CHARGER_INPUT_CURRENT_PCT (if configured), and clamps charge_ma to
 * a lower bound of CONFIG_CHARGER_MIN_INPUT_CURRENT_LIMIT (if configured).
 *
 * @param port PD port.
 * @param supplier Identified CHARGE_SUPPLIER_*.
 * @param charge_ma Desired charge current limit, <= max_ma.
 * @param max_ma Maximum charge current limit, >= charge_ma.
 * @param charge_mv Negotiated charge voltage (mV).
 */
__override_proto void board_set_charge_limit(int port, int supplier,
					     int charge_ma, int max_ma,
					     int charge_mv);

/**
 * Get whether the port is sourcing power on VBUS.
 *
 * @param port PD port.
 * @return VBUS power state.
 */
int board_vbus_source_enabled(int port);

#ifdef CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT
/**
 * Gets the adc_channel for the specified port.
 *
 * @param port PD port.
 * @return adc_channel that measures the Vbus voltage.
 */
enum adc_channel board_get_vbus_adc(int port);
#endif /* CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT */

/**
 * Board specific callback to check if the given port is sink.
 *
 * @param port	Dedicated charge port.
 * @return 1 if the port is sink.
 */
__override_proto int board_charge_port_is_sink(int port);

/**
 * Board specific callback to check if the given port is connected.
 *
 * @param port	Dedicated charge port.
 * @return 1 if the port is connected.
 */
__override_proto int board_charge_port_is_connected(int port);

/**
 * Board specific callback to fill passed power_info structure with current info
 * about the passed dedicate port.
 * This function is responsible for filling r->meas.* and r->max_power.
 *
 * @param port	Dedicated charge port.
 * @param r	USB PD power info to be updated.
 */
__override_proto void
board_fill_source_power_info(int port, struct ec_response_usb_pd_power_info *r);

/**
 * Board specific callback to get vbus voltage.
 *
 * @param port  Dedicated charge port.
 */
__override_proto int board_get_vbus_voltage(int port);

int is_pd_port(int port);

/**
 * Board specific callback to modify the delay time of leaving safe mode
 */
__override_proto int board_get_leave_safe_mode_delay_ms(void);

#endif /* __CROS_EC_CHARGE_MANAGER_H */
