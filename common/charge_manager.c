/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "adc.h"
#include "atomic.h"
#include "battery.h"
#include "builtin/assert.h"
#include "charge_manager.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "dps.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#ifdef CONFIG_ZEPHYR
#include "zephyr/include/usbc/pdc_power_mgmt.h"
#endif

#ifdef HAS_MOCK_CHARGE_MANAGER
#error Mock defined HAS_MOCK_CHARGE_MANAGER
#endif

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ##args)

#define POWER(charge_port) ((charge_port.current) * (charge_port.voltage))

/* Timeout for delayed override power swap, allow for 500ms extra */
#define POWER_SWAP_TIMEOUT \
	(PD_T_SRC_RECOVER_MAX + PD_T_SRC_TURN_ON + PD_T_SAFE_0V + 500 * MSEC)

/*
 * Default charge supplier priority
 *
 * - Always pick dedicated charge if present since that is the best product
 *   decision.
 * - Pick PD negotiated chargers over everything else since they have the most
 *   power potential and they may not currently be negotiated at a high power.
 *   (and they can at least provide 15W)
 * - Pick Type-C which supplier current >= 1.5A, which has higher prioirty
 *   than the BC1.2 and Type-C with current under 1.5A. (USB-C spec 1.3
 *   Table 4-17: TYPEC 3.0A, 1.5A > BC1.2 > TYPEC under 1.5A)
 * - Then pick among the propreitary and BC1.2 chargers which ever has the
 *   highest available power.
 * - Last, pick one from the rest suppliers.  Also note that some boards assume
 *   wireless suppliers as low priority.
 */
__overridable const int supplier_priority[] = {
#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
	[CHARGE_SUPPLIER_DEDICATED] = 0,
#endif
	[CHARGE_SUPPLIER_PD] = 1,
	[CHARGE_SUPPLIER_TYPEC] = 2,
	[CHARGE_SUPPLIER_TYPEC_DTS] = 2,
#ifdef CHARGE_MANAGER_BC12
	[CHARGE_SUPPLIER_PROPRIETARY] = 3,
	[CHARGE_SUPPLIER_BC12_DCP] = 3,
	[CHARGE_SUPPLIER_BC12_CDP] = 3,
	[CHARGE_SUPPLIER_BC12_SDP] = 3,
	[CHARGE_SUPPLIER_TYPEC_UNDER_1_5A] = 4,
	[CHARGE_SUPPLIER_OTHER] = 4,
	[CHARGE_SUPPLIER_VBUS] = 4,
#endif

};
BUILD_ASSERT(ARRAY_SIZE(supplier_priority) == CHARGE_SUPPLIER_COUNT);

const char *charge_supplier_name[] = { CHARGE_SUPPLIER_NAME };
BUILD_ASSERT(ARRAY_SIZE(charge_supplier_name) == CHARGE_SUPPLIER_COUNT);

/* Keep track of available charge for each charge port. */
static struct charge_port_info available_charge[CHARGE_SUPPLIER_COUNT]
					       [CHARGE_PORT_COUNT];

/* Keep track of when the supplier on each port is registered. */
static timestamp_t registration_time[CHARGE_PORT_COUNT];

/*
 * Charge current ceiling (mA) for ports. This can be set to temporarily limit
 * the charge pulled from a port, without influencing the port selection logic.
 * The ceiling can be set independently from several requestors, with the
 * minimum ceiling taking effect.
 */
static int charge_ceil[CHARGE_PORT_COUNT][CEIL_REQUESTOR_COUNT];

/* Dual-role capability of attached device (e.g. AC adapter, mobile battery). */
static enum dualrole_capabilities dualrole_capability[CHARGE_PORT_COUNT];

#ifdef CONFIG_USB_PD_LOGGING
/* Mark port as dirty when making changes, for later logging */
static int save_log[CHARGE_PORT_COUNT];
#endif

/* Store current state of port enable / charge current. */
test_export_static int charge_port = CHARGE_PORT_NONE;
static int charge_current = CHARGE_CURRENT_UNINITIALIZED;
static int charge_current_uncapped = CHARGE_CURRENT_UNINITIALIZED;
static int charge_voltage;
static int charge_supplier = CHARGE_SUPPLIER_NONE;
static int charge_pd_current_uncapped = CHARGE_CURRENT_UNINITIALIZED;
static int override_port = OVERRIDE_OFF;

static int delayed_override_port = OVERRIDE_OFF;
static timestamp_t delayed_override_deadline;

/* Source-out Rp values for TCPMv1 */
__maybe_unused static uint8_t source_port_rp[CONFIG_USB_PD_PORT_MAX_COUNT];

#ifdef CONFIG_USB_PD_MAX_TOTAL_SOURCE_CURRENT
/* 3A on one port and 1.5A on the rest */
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT * 1500 + 1500 <=
	     CONFIG_USB_PD_MAX_TOTAL_SOURCE_CURRENT);
#endif

/*
 * charge_manager initially operates in safe mode until asked to leave (through
 * charge_manager_leave_safe_mode()). While in safe mode, the following
 * behavior is altered:
 *
 * 1) All chargers are considered dedicated (and thus are valid charge source
 *    candidates) for the purpose of port selection.
 * 2) Charge ceilings are ignored. Most significantly, ILIM won't drop on PD
 *    voltage transition. If current load is high during transition, some
 *    chargers may brown-out.
 * 3) CHARGE_PORT_NONE will not be selected (POR default charge port will
 *    remain selected rather than CHARGE_PORT_NONE).
 *
 * After leaving safe mode, charge_manager reverts to its normal behavior and
 * immediately selects charge port and current using standard rules.
 */
#ifdef CONFIG_CHARGE_MANAGER_SAFE_MODE
static int left_safe_mode;
#else
static const int left_safe_mode = 1;
#endif

enum charge_manager_change_type {
	CHANGE_CHARGE,
	CHANGE_DUALROLE,
};

int is_pd_port(int port)
{
	return port >= 0 && port < board_get_usb_pd_port_count();
}

static int is_sink(int port)
{
	if (!is_pd_port(port))
		return board_charge_port_is_sink(port);

	return pd_get_power_role(port) == PD_ROLE_SINK;
}

/**
 * Some of the SKUs in certain boards have less number of USB PD ports than
 * defined in CONFIG_USB_PD_PORT_MAX_COUNT. With the charge port configuration
 * for DEDICATED_PORT towards the end, this will lead to holes in the static
 * configuration. The ports that fall in that hole are invalid and this function
 * is used to check the validity of the ports.
 */
static int is_valid_port(int port)
{
	if (port < 0 || port >= CHARGE_PORT_COUNT)
		return 0;

	/* Check if the port falls in the hole */
	if (port >= board_get_usb_pd_port_count() &&
	    port < CONFIG_USB_PD_PORT_MAX_COUNT)
		return 0;
	return 1;
}

static bool is_valid_override_port(int port)
{
	return (OVERRIDE_DONT_CHARGE <= port) && (port < CHARGE_PORT_COUNT);
}

static int is_connected(int port)
{
	if (!is_pd_port(port))
		return board_charge_port_is_connected(port);

	return pd_is_connected(port);
}

#ifndef CONFIG_CHARGE_MANAGER_DRP_CHARGING
/**
 * In certain cases we need to override the default behavior of not charging
 * from non-dedicated chargers. If the system is in RO and locked, we have no
 * way of determining the actual dualrole capability of the charger because
 * PD communication is not allowed, so we must assume that it is dedicated.
 * Also, if no battery is present, the charger may be our only source of power,
 * so again we must assume that the charger is dedicated.
 *
 * @return	1 when we need to override the a non-dedicated charger
 *		to be a dedicated one, 0 otherwise.
 */
static int charge_manager_spoof_dualrole_capability(void)
{
	return (system_get_image_copy() == EC_IMAGE_RO && system_is_locked()) ||
	       !left_safe_mode;
}
#endif /* !CONFIG_CHARGE_MANAGER_DRP_CHARGING */

/**
 * Initialize available charge. Run before board init, so board init can
 * initialize data, if needed.
 */
static void charge_manager_init(void)
{
	int i, j;

	for (i = 0; i < CHARGE_PORT_COUNT; ++i) {
		if (!is_valid_port(i))
			continue;
		for (j = 0; j < CHARGE_SUPPLIER_COUNT; ++j) {
			available_charge[j][i].current =
				CHARGE_CURRENT_UNINITIALIZED;
			available_charge[j][i].voltage =
				CHARGE_VOLTAGE_UNINITIALIZED;
		}
		for (j = 0; j < CEIL_REQUESTOR_COUNT; ++j)
			charge_ceil[i][j] = CHARGE_CEIL_NONE;
		if (!is_pd_port(i))
			dualrole_capability[i] = CAP_DEDICATED;
		if (is_pd_port(i) && !IS_ENABLED(CONFIG_USB_PD_TCPMV2))
			source_port_rp[i] = CONFIG_USB_PD_PULLUP;
	}
}
#ifndef CONFIG_USB_PDC_POWER_MGMT
DECLARE_HOOK(HOOK_INIT, charge_manager_init, HOOK_PRIO_INIT_CHARGE_MANAGER);
#else
BUILD_ASSERT(CONFIG_CHARGE_MANAGER_SYS_INIT_PRIORITY <
		     CONFIG_PDC_POWER_MGMT_INIT_PRIORITY,
	     "The charge manager initialization must be higher priortity than "
	     "the PDC power management");

/* When CONFIG_USB_PDC_POWER_MGMT is used, we need to init the
 * charge manager before PDC power management subsystem.
 */
static int charge_manager_sys_init(void)
{
	charge_manager_init();
	return 0;
}
SYS_INIT(charge_manager_sys_init, POST_KERNEL,
	 CONFIG_CHARGE_MANAGER_SYS_INIT_PRIORITY);
#endif

/**
 * Check if the charge manager is seeded.
 *
 * @return	1 if all ports/suppliers have reported
 *		with some initial charge, 0 otherwise.
 */
static int charge_manager_is_seeded(void)
{
	/* Once we're seeded, we don't need to check again. */
	static int is_seeded;
	int i, j;

	if (is_seeded)
		return 1;

	for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i) {
		for (j = 0; j < CHARGE_PORT_COUNT; ++j) {
			if (!is_valid_port(j))
				continue;
			if (available_charge[i][j].current ==
				    CHARGE_CURRENT_UNINITIALIZED ||
			    available_charge[i][j].voltage ==
				    CHARGE_VOLTAGE_UNINITIALIZED)
				return 0;
		}
	}
	is_seeded = 1;
	return 1;
}

int charge_manager_get_pd_current_uncapped(void)
{
	return charge_pd_current_uncapped;
}

/**
 * Get the maximum charge current for a port.
 *
 * @param port	Charge port.
 * @return	Charge current (mA).
 */
__maybe_unused static int charge_manager_get_source_current(int port)
{
	if (!is_pd_port(port))
		return 0;

	switch (source_port_rp[port]) {
	case TYPEC_RP_3A0:
		return 3000;
	case TYPEC_RP_1A5:
		return 1500;
	case TYPEC_RP_USB:
	default:
		return 500;
	}
}

/*
 * Find a supplier considering available current, voltage, power, and priority.
 */
static enum charge_supplier find_supplier(int port, enum charge_supplier sup,
					  int min_cur)
{
	int i;
	for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i) {
		if (available_charge[i][port].current <= min_cur ||
		    available_charge[i][port].voltage <= 0)
			/* Doesn't meet volt or current requirement. Skip it. */
			continue;
		if (sup == CHARGE_SUPPLIER_NONE)
			/* Haven't found any yet. Take it unconditionally. */
			sup = i;
		else if (supplier_priority[sup] < supplier_priority[i])
			/* There is already a higher priority supplier. */
			continue;
		else if (supplier_priority[i] < supplier_priority[sup])
			/* This has a higher priority. Take it. */
			sup = i;
		else if (POWER(available_charge[i][port]) >
			 POWER(available_charge[sup][port]))
			/* Priority is tie. Take it if power is higher. */
			sup = i;
	}
	return sup;
}

static enum charge_supplier get_current_supplier(int port)
{
	enum charge_supplier supplier = CHARGE_SUPPLIER_NONE;

	/* Determine supplier information to show. */
	if (port == charge_port) {
		supplier = charge_supplier;
	} else {
		/* Consider available current */
		supplier = find_supplier(port, supplier, 0);
		if (supplier == CHARGE_SUPPLIER_NONE)
			/* Ignore available current */
			supplier = find_supplier(port, supplier, -1);
	}

	return supplier;
}
static enum usb_power_roles
get_current_power_role(int port, enum charge_supplier supplier)
{
	enum usb_power_roles role;
	if (charge_port == port)
		role = USB_PD_PORT_POWER_SINK;
	else if (is_connected(port) && !is_sink(port))
		role = USB_PD_PORT_POWER_SOURCE;
	else if (supplier != CHARGE_SUPPLIER_NONE)
		role = USB_PD_PORT_POWER_SINK_NOT_CHARGING;
	else
		role = USB_PD_PORT_POWER_DISCONNECTED;
	return role;
}

__overridable int board_get_vbus_voltage(int port)
{
	return 0;
}

static int get_vbus_voltage(int port, enum usb_power_roles current_role)
{
	int voltage_mv;

	/*
	 * If we are sourcing power or sinking but not charging, then VBUS must
	 * be 5V. If we are charging, then read VBUS ADC.
	 */
	if (current_role == USB_PD_PORT_POWER_SINK_NOT_CHARGING) {
		voltage_mv = 5000;
	} else {
#if defined(CONFIG_USB_PD_VBUS_MEASURE_CHARGER)
		/*
		 * Try to get VBUS from the charger. If that fails, default to 0
		 * mV.
		 */
		if (charger_get_vbus_voltage(port, &voltage_mv))
			voltage_mv = 0;
#elif defined(CONFIG_USB_PD_VBUS_MEASURE_TCPC)
		voltage_mv = tcpc_get_vbus_voltage(port);
#elif defined(CONFIG_USB_PD_VBUS_MEASURE_ADC_EACH_PORT)
		voltage_mv = adc_read_channel(board_get_vbus_adc(port));
#elif defined(CONFIG_USB_PD_VBUS_MEASURE_NOT_PRESENT)
		/* No VBUS ADC channel - voltage is unknown */
		voltage_mv = 0;
#elif defined(CONFIG_USB_PD_VBUS_MEASURE_BY_BOARD)
		voltage_mv = board_get_vbus_voltage(port);
#elif defined(CONFIG_USB_PD_VBUS_MEASURE_PDC)
		voltage_mv = pdc_power_mgmt_get_vbus_voltage(port);
#else
		/* There is a single ADC that measures joint Vbus */
		voltage_mv = adc_read_channel(ADC_VBUS);
#endif
	}
	return voltage_mv;
}

int charge_manager_get_vbus_voltage(int port)
{
	return get_vbus_voltage(
		port, get_current_power_role(port, get_current_supplier(port)));
}

#ifdef CONFIG_CMD_VBUS
static int command_vbus(int argc, const char **argv)
{
	/* port = -1 to print all the ports */
	int port = -1;
	int vbus, vsys;

	if (argc == 2)
		port = atoi(argv[1]);

	ccprintf("       VBUS     VSYS\n");
	for (int i = 0; i < CHARGE_PORT_COUNT; i++) {
		if (port < 0 || i == port) {
			vbus = charge_manager_get_vbus_voltage(i);
			if (charger_get_vsys_voltage(i, &vsys))
				vsys = -1;
			ccprintf("  P%d %6dmV ", i, vbus);
			if (vsys >= 0)
				ccprintf("%6dmV\n", vsys);
			else
				ccprintf("(unknown)\n");
		}
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(vbus, command_vbus, "[port]",
			"Print VBUS & VSYS of the given port");
#endif

/**
 * Fills passed power_info structure with current info about the passed port.
 *
 * @param port	Charge port.
 * @param r	USB PD power info to be updated.
 */
static void
charge_manager_fill_power_info(int port,
			       struct ec_response_usb_pd_power_info *r)
{
	enum charge_supplier sup = get_current_supplier(port);

	/* Fill in power role */
	r->role = get_current_power_role(port, sup);

	/* Is port partner dual-role capable */
	r->dualrole = (dualrole_capability[port] == CAP_DUALROLE);

	if (sup == CHARGE_SUPPLIER_NONE ||
	    r->role == USB_PD_PORT_POWER_SOURCE) {
		if (is_pd_port(port)) {
			r->type = USB_CHG_TYPE_NONE;
			r->meas.voltage_max = 0;
			r->meas.voltage_now =
				r->role == USB_PD_PORT_POWER_SOURCE ? 5000 : 0;
			/* TCPMv2 tracks source-out current in the DPM */
			if (IS_ENABLED(CONFIG_USB_PD_TCPMV2))
				r->meas.current_max =
					dpm_get_source_current(port);
			else
				r->meas.current_max =
					charge_manager_get_source_current(port);
			r->max_power = 0;
		} else {
			r->type = USB_CHG_TYPE_NONE;
			board_fill_source_power_info(port, r);
		}
	} else {
		uint32_t max_mv, max_ma, pdo, unused;

		switch (sup) {
		case CHARGE_SUPPLIER_PD:
			r->type = USB_CHG_TYPE_PD;
			break;
		case CHARGE_SUPPLIER_TYPEC:
		case CHARGE_SUPPLIER_TYPEC_DTS:
			r->type = USB_CHG_TYPE_C;
			break;
#ifdef CHARGE_MANAGER_BC12
		case CHARGE_SUPPLIER_PROPRIETARY:
			r->type = USB_CHG_TYPE_PROPRIETARY;
			break;
		case CHARGE_SUPPLIER_BC12_DCP:
			r->type = USB_CHG_TYPE_BC12_DCP;
			break;
		case CHARGE_SUPPLIER_BC12_CDP:
			r->type = USB_CHG_TYPE_BC12_CDP;
			break;
		case CHARGE_SUPPLIER_BC12_SDP:
			r->type = USB_CHG_TYPE_BC12_SDP;
			break;
		case CHARGE_SUPPLIER_VBUS:
			r->type = USB_CHG_TYPE_VBUS;
			break;
#endif
#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
		case CHARGE_SUPPLIER_DEDICATED:
			r->type = USB_CHG_TYPE_DEDICATED;
			break;
#endif
		default:
			r->type = USB_CHG_TYPE_OTHER;
		}

		if (IS_ENABLED(CONFIG_USB_PD_DPS) && dps_is_enabled() &&
		    sup == CHARGE_SUPPLIER_PD) {
			/*
			 * Returns the maximum power the system can request when
			 * DPS enabled. This is to prevent the system think it's
			 * using a low power charger.
			 */
			pd_find_pdo_index(pd_get_src_cap_cnt(port),
					  pd_get_src_caps(port),
					  pd_get_max_voltage(), &pdo);
			pd_extract_pdo_power(pdo, &max_ma, &max_mv, &unused);
		} else {
			max_mv = available_charge[sup][port].voltage;
			max_ma = available_charge[sup][port].current;
		}

		r->meas.voltage_max = max_mv;

		/*
		 * Report unknown charger CHARGE_DETECT_DELAY after supplier
		 * change since PD negotiation may take time.
		 *
		 * Do not debounce on batteryless systems because
		 * USB_CHG_TYPE_UNKNOWN implies the system is still on battery
		 * while some kind of negotiation happens, but by the time the
		 * host might request this in a battery-free configuration we
		 * must be stable (if not, the system is either up or about to
		 * lose power again).
		 */
#ifdef CONFIG_BATTERY
		if (get_time().val <
		    registration_time[port].val + CHARGE_DETECT_DELAY)
			r->type = USB_CHG_TYPE_UNKNOWN;
#endif

#if defined(HAS_TASK_CHG_RAMP) || defined(CONFIG_CHARGE_RAMP_HW)
		if ((charge_port == port) && chg_ramp_allowed(port, sup)) {
			/* Current limit is output of ramp module */
			r->meas.current_lim = chg_ramp_get_current_limit();

			/*
			 * If ramp is allowed, then the max current depends
			 * on if ramp is stable. If ramp is stable, then
			 * max current is same as input current limit. If
			 * ramp is not stable, then we report the maximum
			 * current we could ramp up to for this supplier.
			 * If ramp is not allowed, max current is just the
			 * available charge current.
			 */
			r->meas.current_max =
				chg_ramp_is_stable() ?
					r->meas.current_lim :
					chg_ramp_max(port, sup, max_ma);
		} else {
			r->meas.current_max = r->meas.current_lim = max_ma;
		}
#else
		r->meas.current_max = r->meas.current_lim = max_ma;
#endif
		r->max_power = r->meas.current_max * r->meas.voltage_max;

		r->meas.voltage_now = get_vbus_voltage(port, r->role);
	}
}

#ifdef CONFIG_USB_PD_LOGGING
/**
 * Saves a power state log entry with the current info about the passed port.
 */
void charge_manager_save_log(int port)
{
	uint16_t flags = 0;
	struct ec_response_usb_pd_power_info pinfo;

	if (!is_pd_port(port))
		return;

	save_log[port] = 0;
	charge_manager_fill_power_info(port, &pinfo);

	/* Flags are stored in the data field */
	if (port == override_port)
		flags |= CHARGE_FLAGS_OVERRIDE;
	if (port == delayed_override_port)
		flags |= CHARGE_FLAGS_DELAYED_OVERRIDE;
	flags |= pinfo.role | (pinfo.type << CHARGE_FLAGS_TYPE_SHIFT) |
		 (pinfo.dualrole ? CHARGE_FLAGS_DUAL_ROLE : 0);

	pd_log_event(PD_EVENT_MCU_CHARGE,
		     PD_LOG_PORT_SIZE(port, sizeof(pinfo.meas)), flags,
		     &pinfo.meas);
}
#endif /* CONFIG_USB_PD_LOGGING */

/**
 * Attempt to switch to power source on port if applicable.
 *
 * @param port	USB-C port to be swapped.
 */
static void charge_manager_switch_to_source(int port)
{
	if (!is_pd_port(port))
		return;

	/* If connected to dual-role device, then ask for a swap */
	if (dualrole_capability[port] == CAP_DUALROLE && is_sink(port))
		pd_request_power_swap(port);
}

/**
 * Return the computed charge ceiling for a port, which represents the
 * minimum ceiling among all valid requestors.
 *
 * @param port	Charge port.
 * @return	Charge ceiling (mA) or CHARGE_CEIL_NONE.
 */
static int charge_manager_get_ceil(int port)
{
	int ceil = CHARGE_CEIL_NONE;
	int val, i;

	if (!is_valid_port(port))
		return ceil;

	for (i = 0; i < CEIL_REQUESTOR_COUNT; ++i) {
		val = charge_ceil[port][i];
		if (val != CHARGE_CEIL_NONE &&
		    (ceil == CHARGE_CEIL_NONE || val < ceil))
			ceil = val;
	}

	return ceil;
}

/**
 * Select the best charge port or the override port, as defined by the supplier
 * hierarchy and the available power.
 *
 * @param new_port	Pointer to the selected port.
 * @param new_supplier	Pointer to the selected supplier.
 */
static void charge_manager_get_best_port(int *new_port, int *new_supplier)
{
	int supplier = CHARGE_SUPPLIER_NONE;
	int port = CHARGE_PORT_NONE;
	int best_port_power = -1, candidate_port_power;
	int i, j;

	if (override_port == OVERRIDE_DONT_CHARGE) {
		*new_port = CHARGE_PORT_NONE;
		*new_supplier = CHARGE_SUPPLIER_NONE;
		return;
	}

	/*
	 * Charge supplier selection logic:
	 * 1. Prefer DPS charge port.
	 * 2. Prefer higher priority supply.
	 * 3. Prefer higher power over lower in case priority is tied.
	 * 4. Prefer current charge port over new port in case (1)
	 *    and (2) are tied.
	 * available_charge can be changed at any time by other tasks,
	 * so make no assumptions about its consistency.
	 */
	for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i) {
		for (j = 0; j < CHARGE_PORT_COUNT; ++j) {
			/* Skip this port if it is not valid. */
			if (!is_valid_port(j))
				continue;

			/*
			 * Skip this supplier if there is no
			 * available charge.
			 */
			if (available_charge[i][j].current == 0 ||
			    available_charge[i][j].voltage == 0)
				continue;

			/*
			 * Don't select this port if we have a
			 * charge on another override port.
			 */
			if (override_port != OVERRIDE_OFF &&
			    override_port == port && override_port != j)
				continue;

#ifndef CONFIG_CHARGE_MANAGER_DRP_CHARGING
			/*
			 * Don't charge from a dual-role port unless
			 * it is our override port.
			 */
			if (dualrole_capability[j] != CAP_DEDICATED &&
			    override_port != j &&
			    !charge_manager_spoof_dualrole_capability())
				continue;
#endif

			candidate_port_power = POWER(available_charge[i][j]);

			/* Select DPS port if provided. */
			if (IS_ENABLED(CONFIG_USB_PD_DPS) &&
			    override_port == OVERRIDE_OFF &&
			    i == CHARGE_SUPPLIER_PD &&
			    j == dps_get_charge_port()) {
				supplier = i;
				port = j;
				break;
				/* Select if no supplier chosen yet. */
			} else if (supplier == CHARGE_SUPPLIER_NONE ||
				   /* ..or if supplier priority is higher. */
				   supplier_priority[i] <
					   supplier_priority[supplier] ||
				   /* ..or if this is our override port. */
				   (j == override_port &&
				    port != override_port) ||
				   /* ..or if priority is tied and.. */
				   (supplier_priority[i] ==
					    supplier_priority[supplier] &&
				    /*
				     * candidate port can supply more power or
				     * ..
				     */
				    (candidate_port_power > best_port_power ||
				     /*
				      * candidate port is the active
				      * port and can supply the same
				      * amount of power.
				      */
				     (candidate_port_power == best_port_power &&
				      charge_port == j)))) {
				supplier = i;
				port = j;
				best_port_power = candidate_port_power;
			}
		}
	}

#ifdef CONFIG_BATTERY
	/*
	 * if no battery present then retain same charge port
	 * and charge supplier to avoid the port switching
	 */
	if (charge_port != CHARGE_PORT_NONE && charge_port != port &&
	    (battery_is_present() == BP_NO ||
	     (battery_is_present() == BP_YES &&
	      battery_is_cut_off() != BATTERY_CUTOFF_STATE_NORMAL))) {
		port = charge_port;
		supplier = charge_supplier;
	}
#endif

	*new_port = port;
	*new_supplier = supplier;
}

/**
 * Charge manager refresh -- responsible for selecting the active charge port
 * and charge power. Called as a deferred task.
 */
static void charge_manager_refresh(void)
{
	/* Always initialize charge port on first pass */
	static int active_charge_port_initialized;
	int new_supplier, new_port;
	int new_charge_current, new_charge_current_uncapped;
	int new_charge_voltage, i;
	int updated_new_port = CHARGE_PORT_NONE;
	int updated_old_port = CHARGE_PORT_NONE;
	int ceil;
	int power_changed = 0;

	/* Hunt for an acceptable charge port */
	while (1) {
		charge_manager_get_best_port(&new_port, &new_supplier);

		if (!left_safe_mode && new_port == CHARGE_PORT_NONE)
			return;

		/*
		 * If the port and the supplier are the same, don't (attempt to)
		 * switch to the port unless active charge port hasn't been set.
		 */
		if (active_charge_port_initialized && new_port == charge_port &&
		    new_supplier == charge_supplier)
			break;

		/*
		 * For OCPC systems, reset the OCPC state to prevent current
		 * spikes.
		 */
		if (IS_ENABLED(CONFIG_OCPC)) {
			charge_set_active_chg_chip(new_port);
			trigger_ocpc_reset();
		}

		/*
		 * A different port or a supplier was selected. Make an attempt
		 * to switch to the port.
		 */
		if (board_set_active_charge_port(new_port) == EC_SUCCESS) {
			if (IS_ENABLED(CONFIG_EXTPOWER))
				board_check_extpower();
			break;
		}

		/* 'Dont charge' request must be accepted. */
		ASSERT(new_port != CHARGE_PORT_NONE);

		/*
		 * The board rejected the offered port & supplier. Clear the
		 * available charge on the rejected port so that it is no longer
		 * chosen.
		 */
		for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i) {
			available_charge[i][new_port].current = 0;
			available_charge[i][new_port].voltage = 0;
		}
	}

	active_charge_port_initialized = 1;

	/*
	 * Clear override if it wasn't selected -- it means that no charge is
	 * available on the port, or the port was rejected.
	 */
	if (override_port >= 0 && override_port != new_port)
		override_port = OVERRIDE_OFF;

	if (new_supplier == CHARGE_SUPPLIER_NONE) {
#ifdef CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT
		new_charge_current = CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT;
#else
		new_charge_current = 0;
#endif
		new_charge_current_uncapped = new_charge_current;
		new_charge_voltage = 0;
	} else {
		new_charge_current_uncapped =
			available_charge[new_supplier][new_port].current;
#ifdef CONFIG_CHARGE_RAMP_HW
		/*
		 * Allow to set the maximum current value, so the hardware can
		 * know the range of acceptable current values for its ramping.
		 */
		if (chg_ramp_allowed(new_port, new_supplier))
			new_charge_current_uncapped =
				chg_ramp_max(new_port, new_supplier,
					     new_charge_current_uncapped);
#endif /* CONFIG_CHARGE_RAMP_HW */
		/* Enforce port charge ceiling. */
		ceil = charge_manager_get_ceil(new_port);
		if (left_safe_mode && ceil != CHARGE_CEIL_NONE)
			new_charge_current =
				MIN(ceil, new_charge_current_uncapped);
		else
			new_charge_current = new_charge_current_uncapped;

		new_charge_voltage =
			available_charge[new_supplier][new_port].voltage;
	}

	/*
	 * Record the PD current limit to prevent from over-sinking
	 * the charger.
	 */
	if (new_supplier == CHARGE_SUPPLIER_PD)
		charge_pd_current_uncapped = new_charge_current_uncapped;
	else
		charge_pd_current_uncapped = CHARGE_CURRENT_UNINITIALIZED;

	/* Change the charge limit + charge port/supplier if modified. */
	if (new_port != charge_port || new_charge_current != charge_current ||
	    new_supplier != charge_supplier) {
#ifdef HAS_TASK_CHG_RAMP
		chg_ramp_charge_supplier_change(new_port, new_supplier,
						new_charge_current,
						registration_time[new_port],
						new_charge_voltage);
#else
#ifdef CONFIG_CHARGE_RAMP_HW
		/* Enable or disable charge ramp */
		charger_set_hw_ramp(chg_ramp_allowed(new_port, new_supplier));
#endif
		board_set_charge_limit(new_port, new_supplier,
				       new_charge_current,
				       new_charge_current_uncapped,
				       new_charge_voltage);
#endif /* HAS_TASK_CHG_RAMP */

		power_changed = 1;

		CPRINTS("CL: p%d s%d i%d v%d", new_port, new_supplier,
			new_charge_current, new_charge_voltage);

		/*
		 * (b:192638664) We try to check AC OK again to avoid
		 * unsuccessful detection in the initial detection.
		 */
		if (IS_ENABLED(CONFIG_EXTPOWER))
			board_check_extpower();
	}

	/*
	 * Signal new power request only if the port changed, the voltage
	 * on the same port changed, or the actual uncapped current
	 * on the same port changed (don't consider ceil).
	 */
	if (new_port != CHARGE_PORT_NONE &&
	    (new_port != charge_port ||
	     new_charge_current_uncapped != charge_current_uncapped ||
	     new_charge_voltage != charge_voltage))
		updated_new_port = new_port;

	/* If charge port changed, cleanup old port */
	if (charge_port != new_port && charge_port != CHARGE_PORT_NONE) {
		/* Check if need power swap */
		charge_manager_switch_to_source(charge_port);
		/* Signal new power request on old port */
		updated_old_port = charge_port;
	}

	/* Update globals to reflect current state. */
	charge_current = new_charge_current;
	charge_current_uncapped = new_charge_current_uncapped;
	charge_voltage = new_charge_voltage;
	charge_supplier = new_supplier;
	charge_port = new_port;

#ifdef CONFIG_USB_PD_LOGGING
	/*
	 * Write a log under the following conditions:
	 *  1. A port becomes active or
	 *  2. A port becomes inactive or
	 *  3. The active charge port power limit changes or
	 *  4. Any supplier change on an inactive port
	 */
	if (updated_new_port != CHARGE_PORT_NONE)
		save_log[updated_new_port] = 1;
	/* Don't log non-meaningful changes on charge port */
	else if (charge_port != CHARGE_PORT_NONE)
		save_log[charge_port] = 0;

	if (updated_old_port != CHARGE_PORT_NONE)
		save_log[updated_old_port] = 1;

	for (i = 0; i < board_get_usb_pd_port_count(); ++i)
		if (save_log[i])
			charge_manager_save_log(i);
#endif

	/* New power requests must be set only after updating the globals. */
	if (is_pd_port(updated_new_port)) {
		/* Check if we can get requested voltage/current */
		if ((IS_ENABLED(CONFIG_USB_PD_TCPMV1) &&
		     IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE)) ||
		    (IS_ENABLED(CONFIG_USB_PD_TCPMV2) &&
		     IS_ENABLED(CONFIG_USB_PE_SM))) {
			uint32_t pdo;
			uint32_t max_voltage;
			uint32_t max_current;
			uint32_t unused;
			bool new_req = false;
			/*
			 * Check if new voltage/current is different
			 * than requested. If yes, send new power request
			 */
			if (pd_get_requested_voltage(updated_new_port) !=
				    charge_voltage ||
			    pd_get_requested_current(updated_new_port) !=
				    charge_current_uncapped)
				new_req = true;

			if (IS_ENABLED(CONFIG_USB_PD_DPS) && dps_is_enabled()) {
				/* Fall-through. DPS control sink voltage */
			} else {
				/*
				 * Check if we can get more power from this
				 * port. If yes, send new power request
				 */
				pd_find_pdo_index(
					pd_get_src_cap_cnt(updated_new_port),
					pd_get_src_caps(updated_new_port),
					pd_get_max_voltage(), &pdo);
				pd_extract_pdo_power(pdo, &max_current,
						     &max_voltage, &unused);

				if (charge_voltage != max_voltage ||
				    charge_current_uncapped != max_current)
					new_req = true;
			}

			if (new_req)
				pd_set_new_power_request(updated_new_port);
		} else {
			/*
			 * Functions for getting requested voltage/current
			 * are not available. Send new power request.
			 */
			pd_set_new_power_request(updated_new_port);
		}
	}
	if (is_pd_port(updated_old_port))
		pd_set_new_power_request(updated_old_port);

	if (power_changed) {
		hook_notify(HOOK_POWER_SUPPLY_CHANGE);
		/* notify host of power info change */
		pd_send_host_event(PD_EVENT_POWER_CHANGE);
	}
}
DECLARE_DEFERRED(charge_manager_refresh);

/**
 * Called when charge override times out waiting for power swap.
 */
static void charge_override_timeout(void)
{
	delayed_override_port = OVERRIDE_OFF;
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}
DECLARE_DEFERRED(charge_override_timeout);

/**
 * Called CHARGE_DETECT_DELAY after the most recent charge change on a port.
 */
static void charger_detect_debounced(void)
{
	/* Inform host that charger detection is debounced. */
	pd_send_host_event(PD_EVENT_POWER_CHANGE);
}
DECLARE_DEFERRED(charger_detect_debounced);

/**
 * Update charge parameters for a given port / supplier.
 *
 * @param change		Type of change.
 * @param supplier		Charge supplier to be updated.
 * @param port			Charge port to be updated.
 * @param charge		Charge port current / voltage.
 */
static void charge_manager_make_change(enum charge_manager_change_type change,
				       int supplier, int port,
				       const struct charge_port_info *charge)
{
	int i;
	int clear_override = 0;

	if (!is_valid_port(port)) {
		CPRINTS("%s: p%d invalid", __func__, port);
		return;
	}

	/* Determine if this is a change which can affect charge status */
	switch (change) {
	case CHANGE_CHARGE:
		/* Ignore changes where charge is identical */
		if (available_charge[supplier][port].current ==
			    charge->current &&
		    available_charge[supplier][port].voltage == charge->voltage)
			return;
		if (charge->current > 0 &&
		    available_charge[supplier][port].current == 0)
			/* A charger is plugged. Clear override. */
			clear_override = 1;
#ifdef CONFIG_USB_PD_LOGGING
		save_log[port] = 1;
#endif
		break;
	case CHANGE_DUALROLE:
		/*
		 * Ignore all except for transition to non-dualrole,
		 * which may occur some time after we see a charge
		 */
#ifndef CONFIG_CHARGE_MANAGER_DRP_CHARGING
		if (dualrole_capability[port] != CAP_DEDICATED)
#endif
			return;
		/* Clear override only if a charge is present on the port */
		for (i = 0; i < CHARGE_SUPPLIER_COUNT; ++i)
			if (available_charge[i][port].current > 0) {
				clear_override = 1;
				break;
			}
		/*
		 * If there is no charge present on the port, the dualrole
		 * change is meaningless to charge_manager.
		 */
		if (!clear_override)
			return;
		break;
	}

	/* Remove override when a charger is plugged */
	if (clear_override && override_port != port &&
	    override_port != OVERRIDE_DONT_CHARGE
#ifndef CONFIG_CHARGE_MANAGER_DRP_CHARGING
	    /* only remove override when it's a dedicated charger */
	    && dualrole_capability[port] == CAP_DEDICATED
#endif
	) {
		override_port = OVERRIDE_OFF;
		if (delayed_override_port != OVERRIDE_OFF) {
			delayed_override_port = OVERRIDE_OFF;
			hook_call_deferred(&charge_override_timeout_data, -1);
		}
	}

	if (change == CHANGE_CHARGE) {
		available_charge[supplier][port].current = charge->current;
		available_charge[supplier][port].voltage = charge->voltage;
		registration_time[port] = get_time();

		/*
		 * After CHARGE_DETECT_DELAY, inform the host that charger
		 * detection has been debounced. Since only one deferred
		 * routine exists for all ports, the deferred call for a given
		 * port may potentially be cancelled. This is mostly harmless
		 * since cancellation implies that PD_EVENT_POWER_CHANGE was
		 * just sent due to the power change on another port.
		 */
		if (charge->current > 0)
			hook_call_deferred(&charger_detect_debounced_data,
					   CHARGE_DETECT_DELAY);

		/*
		 * If we have a charge on our delayed override port within
		 * the deadline, make it our override port.
		 */
		if (port == delayed_override_port && charge->current > 0 &&
		    is_sink(delayed_override_port) &&
		    get_time().val < delayed_override_deadline.val) {
			delayed_override_port = OVERRIDE_OFF;
			hook_call_deferred(&charge_override_timeout_data, -1);
			charge_manager_set_override(port);
		}
	}

	/*
	 * Don't call charge_manager_refresh unless all ports +
	 * suppliers have reported in. We don't want to make changes
	 * to our charge port until we are certain we know what is
	 * attached.
	 */
	if (charge_manager_is_seeded())
		hook_call_deferred(&charge_manager_refresh_data, 0);
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
				uint32_t supply_voltage)
{
	struct charge_port_info charge;

	charge.current = max_ma;
	charge.voltage = supply_voltage;
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, port, &charge);
}

void typec_set_input_current_limit(int port, typec_current_t max_ma,
				   uint32_t supply_voltage)
{
	struct charge_port_info charge;
	int i;
	int supplier;
	int dts = !!(max_ma & TYPEC_CURRENT_DTS_MASK);
	static const enum charge_supplier typec_suppliers[] = {
		CHARGE_SUPPLIER_TYPEC,
		CHARGE_SUPPLIER_TYPEC_DTS,
#ifdef CHARGE_MANAGER_BC12
		CHARGE_SUPPLIER_TYPEC_UNDER_1_5A,
#endif /* CHARGE_MANAGER_BC12 */
	};

	charge.current = max_ma & TYPEC_CURRENT_ILIM_MASK;
	charge.voltage = supply_voltage;
#if !defined(HAS_TASK_CHG_RAMP) && !defined(CONFIG_CHARGE_RAMP_HW)
	/*
	 * DTS sources such as suzy-q may not be able to actually deliver
	 * their advertised current, so limit it to reduce chance of OC,
	 * if we can't ramp.
	 */
	if (dts)
		charge.current = MIN(charge.current, 500);
#endif

	supplier = dts ? CHARGE_SUPPLIER_TYPEC_DTS : CHARGE_SUPPLIER_TYPEC;

#ifdef CHARGE_MANAGER_BC12
	/*
	 * According to USB-C spec 1.3 Table 4-17 "Precedence of power source
	 * usage", the priority should be: USB-C 3.0A, 1.5A > BC1.2 > USB-C
	 * under 1.5A.  Choosed the corresponding supplier type, according to
	 * charge current, to update.
	 */
	if (charge.current < 1500)
		supplier = CHARGE_SUPPLIER_TYPEC_UNDER_1_5A;
#endif /* CHARGE_MANAGER_BC12 */

	charge_manager_update_charge(supplier, port, &charge);

	/*
	 * TYPEC / TYPEC-DTS / TYPEC-UNDER_1_5A should be mutually exclusive.
	 * Zero'ing all the other suppliers.
	 */
	for (i = 0; i < ARRAY_SIZE(typec_suppliers); ++i)
		if (supplier != typec_suppliers[i])
			charge_manager_update_charge(typec_suppliers[i], port,
						     NULL);
}

void charge_manager_update_charge(int supplier, int port,
				  const struct charge_port_info *charge)
{
	struct charge_port_info zero = { 0 };
	if (!charge)
		charge = &zero;
	charge_manager_make_change(CHANGE_CHARGE, supplier, port, charge);
}

void charge_manager_update_dualrole(int port, enum dualrole_capabilities cap)
{
	if (!is_pd_port(port))
		return;

	/* Ignore when capability is unchanged */
	if (cap != dualrole_capability[port]) {
		dualrole_capability[port] = cap;
		charge_manager_make_change(CHANGE_DUALROLE, 0, port, NULL);
	}
}

#ifdef CONFIG_CHARGE_MANAGER_SAFE_MODE
__overridable int board_get_leave_safe_mode_delay_ms(void)
{
	return 500;
}

void charge_manager_leave_safe_mode(void)
{
	if (left_safe_mode)
		return;

	/*
	 * Sometimes the fuel gauge will report that it has
	 * sufficient state of charge and remaining capacity,
	 * but in actuality it doesn't.  When the EC sees that
	 * information, it trusts it and leaves charge manager
	 * safe mode.  Doing so will allow CHARGE_PORT_NONE to
	 * be selected, thereby cutting off the input FETs.
	 * When the battery cannot provide the charge it claims,
	 * the system loses power, shuts down, and the battery
	 * is not charged even though the charger is plugged in.
	 * By waiting 500ms, we can avoid the selection of
	 * CHARGE_PORT_NONE around init time and not cut off the
	 * input FETs.
	 */
	crec_msleep(board_get_leave_safe_mode_delay_ms());
	CPRINTS("%s()", __func__);
	cflush();
	left_safe_mode = 1;
	if (charge_manager_is_seeded())
		hook_call_deferred(&charge_manager_refresh_data, 0);
}
#endif

void charge_manager_set_ceil(int port, enum ceil_requestor requestor, int ceil)
{
	if (!is_valid_port(port))
		return;

	if (charge_ceil[port][requestor] != ceil) {
		charge_ceil[port][requestor] = ceil;
		if (port == charge_port && charge_manager_is_seeded())
			hook_call_deferred(&charge_manager_refresh_data, 0);
	}
}

void charge_manager_force_ceil(int port, int ceil)
{
	/*
	 * Force our input current to ceil if we're exceeding it, without
	 * waiting for our deferred task to run.
	 */
	if (left_safe_mode && port == charge_port && ceil < charge_current) {
		board_set_charge_limit(port, CHARGE_SUPPLIER_PD, ceil,
				       charge_current_uncapped, charge_voltage);
		/* Enforcing charge_ceil here prevents race conditions between
		 * the hook task and the PD task, which could occur if
		 * charge_ceil is changed while a deferred
		 * charge_manager_refresh is in process.
		 * charge_manager_force_ceil's premise as an API is to lower
		 * the ceil for the current charge port. It doesn't need to find
		 * a better port, supplier, or PDO. That means most of
		 * charge_manager_refresh doesn't need to be called.
		 */
		charge_current = ceil;
		charge_ceil[port][CEIL_REQUESTOR_PD] = ceil;
		CPRINTS("CL: p%d s%d i%d v%d (forced)", port,
			CHARGE_SUPPLIER_PD, charge_current, charge_voltage);
	} else {
		/*
		 * Inform charge_manager so it stays in sync with the state
		 * of the world.
		 */
		charge_manager_set_ceil(port, CEIL_REQUESTOR_PD, ceil);
	}
}

int charge_manager_set_override(int port)
{
	int retval = EC_SUCCESS;

	CPRINTS("Charge Override: %d", port);

	/*
	 * If attempting to change the override port, then return
	 * error. Since we may be in the middle of a power swap on
	 * the original override port, it's too complicated to
	 * guarantee that the original override port is switched back
	 * to source.
	 */
	if (delayed_override_port != OVERRIDE_OFF)
		return EC_ERROR_BUSY;

	/* Set the override port if it's a sink. */
	if (port < 0 || is_sink(port)) {
		if (override_port != port) {
			override_port = port;
			if (charge_manager_is_seeded())
				hook_call_deferred(&charge_manager_refresh_data,
						   0);
		}
	}
	/*
	 * If the attached device is capable of being a sink, request a
	 * power swap and set the delayed override for swap completion.
	 */
	else if (!is_sink(port) && dualrole_capability[port] == CAP_DUALROLE) {
		delayed_override_deadline.val =
			get_time().val + POWER_SWAP_TIMEOUT;
		delayed_override_port = port;
		hook_call_deferred(&charge_override_timeout_data,
				   POWER_SWAP_TIMEOUT);
		pd_request_power_swap(port);
		/* Can't charge from requested port -- return error. */
	} else
		retval = EC_ERROR_INVAL;

	return retval;
}

int charge_manager_get_override(void)
{
	return override_port;
}

int charge_manager_get_active_charge_port(void)
{
	return charge_port;
}

int charge_manager_get_selected_charge_port(void)
{
	int port, supplier;

	charge_manager_get_best_port(&port, &supplier);
	return port;
}

int charge_manager_get_charger_current(void)
{
	return charge_current;
}

int charge_manager_get_charger_voltage(void)
{
	return charge_voltage;
}

enum charge_supplier charge_manager_get_supplier(void)
{
	return charge_supplier;
}

int charge_manager_get_power_limit_uw(void)
{
	int current_ma = charge_current;
	int voltage_mv = charge_voltage;

	if (current_ma == CHARGE_CURRENT_UNINITIALIZED ||
	    voltage_mv == CHARGE_VOLTAGE_UNINITIALIZED)
		return 0;
	else
		return current_ma * voltage_mv;
}

#if defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT) && \
	!defined(CONFIG_USB_PD_TCPMV2)
/* Note: this functionality is a part of the TCPMv2 Device Poicy Manager */

/* Bitmap of ports used as power source */
static volatile uint32_t source_port_bitmap;
BUILD_ASSERT(sizeof(source_port_bitmap) * 8 >= CONFIG_USB_PD_PORT_MAX_COUNT);

static inline int has_other_active_source(int port)
{
	return source_port_bitmap & ~BIT(port);
}

static inline int is_active_source(int port)
{
	return source_port_bitmap & BIT(port);
}

static int can_supply_max_current(int port)
{
#ifdef CONFIG_USB_PD_MAX_TOTAL_SOURCE_CURRENT
	/*
	 * This guarantees active 3A source continues to supply 3A.
	 *
	 * Since redistribution occurs sequentially, younger ports get
	 * priority. Priority surfaces only when 3A source is released.
	 * That is, when 3A source is released, the youngest active
	 * port gets 3A.
	 */
	int p;
	if (!is_active_source(port))
		/* Non-active ports don't get 3A */
		return 0;
	for (p = 0; p < board_get_usb_pd_port_count(); p++) {
		if (p == port)
			continue;
		if (source_port_rp[p] ==
		    CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
			return 0;
	}
	return 1;
#else
	return is_active_source(port) && !has_other_active_source(port);
#endif /* CONFIG_USB_PD_MAX_TOTAL_SOURCE_CURRENT */
}

void charge_manager_source_port(int port, int enable)
{
	uint32_t prev_bitmap = source_port_bitmap;
	int p, rp;

	if (enable)
		atomic_or((atomic_t *)&source_port_bitmap, 1 << port);
	else
		atomic_clear_bits((atomic_t *)&source_port_bitmap, 1 << port);

	/* No change, exit early. */
	if (prev_bitmap == source_port_bitmap)
		return;

	/* Set port limit according to policy */
	for (p = 0; p < board_get_usb_pd_port_count(); p++) {
		rp = can_supply_max_current(p) ?
			     CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT :
			     CONFIG_USB_PD_PULLUP;
		source_port_rp[p] = rp;

#ifdef CONFIG_USB_PD_LOGGING
		if (is_connected(p) && !is_sink(p))
			charge_manager_save_log(p);
#endif

		typec_set_source_current_limit(p, rp);
		if (IS_ENABLED(CONFIG_USB_PD_TCPMV2))
			typec_select_src_current_limit_rp(p, rp);
		else
			tcpm_select_rp_value(p, rp);
		pd_update_contract(p);
	}
}

int charge_manager_get_source_pdo(const uint32_t **src_pdo, const int port)
{
	if (can_supply_max_current(port)) {
		*src_pdo = pd_src_pdo_max;
		return pd_src_pdo_max_cnt;
	}

	*src_pdo = pd_src_pdo;
	return pd_src_pdo_cnt;
}
#endif /* CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT && !CONFIG_USB_PD_TCPMV2 */

static enum ec_status hc_pd_power_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_power_info *p = args->params;
	struct ec_response_usb_pd_power_info *r = args->response;
	int port = p->port;

	/* If host is asking for the charging port, set port appropriately */
	if (port == PD_POWER_CHARGING_PORT)
		port = charge_port;

	/*
	 * Not checking for invalid port here, because it might break existing
	 * contract with ectool users. The invalid ports will have the response
	 * voltage, current and power parameters set to 0.
	 */
	if (port >= CHARGE_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	charge_manager_fill_power_info(port, r);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_POWER_INFO, hc_pd_power_info,
		     EC_VER_MASK(0));

static enum ec_status hc_charge_port_count(struct host_cmd_handler_args *args)
{
	struct ec_response_charge_port_count *resp = args->response;

	args->response_size = sizeof(*resp);
	resp->port_count = CHARGE_PORT_COUNT;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_PORT_COUNT, hc_charge_port_count,
		     EC_VER_MASK(0));

static enum ec_status
hc_charge_port_override(struct host_cmd_handler_args *args)
{
	const struct ec_params_charge_port_override *p = args->params;
	const int16_t op = p->override_port;

	if (!is_valid_override_port(op))
		return EC_RES_INVALID_PARAM;

	return charge_manager_set_override(op) == EC_SUCCESS ? EC_RES_SUCCESS :
							       EC_RES_ERROR;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_CHARGE_PORT_OVERRIDE, hc_charge_port_override,
		     EC_VER_MASK(0));

#if CONFIG_DEDICATED_CHARGE_PORT_COUNT > 0
static enum ec_status
hc_override_dedicated_charger_limit(struct host_cmd_handler_args *args)
{
	const struct ec_params_dedicated_charger_limit *p = args->params;
	struct charge_port_info ci = {
		.current = p->current_lim,
		.voltage = p->voltage_lim,
	};

	/*
	 * Allow a change only if the dedicated charge port is used. Host needs
	 * to apply a change every time a dedicated charger is plugged.
	 */
	if (charge_port != DEDICATED_CHARGE_PORT)
		return EC_RES_UNAVAILABLE;

	charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
				     DEDICATED_CHARGE_PORT, &ci);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_OVERRIDE_DEDICATED_CHARGER_LIMIT,
		     hc_override_dedicated_charger_limit, EC_VER_MASK(0));
#endif

static int command_charge_port_override(int argc, const char **argv)
{
	int port = OVERRIDE_OFF;
	int ret = EC_SUCCESS;
	char *e;

	if (argc >= 2) {
		port = strtoi(argv[1], &e, 0);
		if (*e || !is_valid_override_port(port))
			return EC_ERROR_PARAM1;
		ret = charge_manager_set_override(port);
	}

	ccprintf("Override: %d\n",
		 (argc >= 2 && ret == EC_SUCCESS) ? port : override_port);
	return ret;
}
DECLARE_CONSOLE_COMMAND(
	chgoverride, command_charge_port_override, "[port | -1 | -2]",
	"Force charging from a given port (-1 = off, -2 = disable charging)");

#ifdef CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT
static void charge_manager_set_external_power_limit(int current_lim,
						    int voltage_lim)
{
	int port;

	if (current_lim == EC_POWER_LIMIT_NONE)
		current_lim = CHARGE_CEIL_NONE;
	if (voltage_lim == EC_POWER_LIMIT_NONE)
		voltage_lim = PD_MAX_VOLTAGE_MV;

	for (port = 0; port < board_get_usb_pd_port_count(); ++port) {
		charge_manager_set_ceil(port, CEIL_REQUESTOR_HOST, current_lim);
		pd_set_external_voltage_limit(port, voltage_lim);
	}
}

/*
 * On transition out of S0, disable all external power limits, in case AP
 * failed to clear them.
 */
static void charge_manager_external_power_limit_off(void)
{
	charge_manager_set_external_power_limit(EC_POWER_LIMIT_NONE,
						EC_POWER_LIMIT_NONE);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, charge_manager_external_power_limit_off,
	     HOOK_PRIO_DEFAULT);

static enum ec_status
hc_external_power_limit(struct host_cmd_handler_args *args)
{
	const struct ec_params_external_power_limit_v1 *p = args->params;

	charge_manager_set_external_power_limit(p->current_lim, p->voltage_lim);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_EXTERNAL_POWER_LIMIT, hc_external_power_limit,
		     EC_VER_MASK(1));

static int command_external_power_limit(int argc, const char **argv)
{
	int max_current;
	int max_voltage;
	char *e;

	if (argc >= 2) {
		max_current = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	} else
		max_current = EC_POWER_LIMIT_NONE;

	if (argc >= 3) {
		max_voltage = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	} else
		max_voltage = EC_POWER_LIMIT_NONE;

	charge_manager_set_external_power_limit(max_current, max_voltage);
	ccprintf("max req: %dmA %dmV\n", max_current, max_voltage);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chglim, command_external_power_limit,
			"[max_current (mA)] [max_voltage (mV)]",
			"Set max charger current / voltage");
#endif /* CONFIG_CHARGE_MANAGER_EXTERNAL_POWER_LIMIT */

#ifdef CONFIG_CMD_CHARGE_SUPPLIER_INFO
static int charge_supplier_info(int argc, const char **argv)
{
	int p, s;
	int port_printed;

	ccprintf("\n");
	ccprintf("Port --Supplier-- Prio -Available Power-\n");
	for (p = 0; p < CHARGE_PORT_COUNT; p++) {
		port_printed = 0;
		for (s = 0; s < CHARGE_SUPPLIER_COUNT; s++) {
			if (available_charge[s][p].current == 0 &&
			    available_charge[s][p].voltage == 0)
				continue;
			if (charge_manager_get_active_charge_port() == p &&
			    charge_manager_get_supplier() == s)
				ccprintf("*");
			else
				ccprintf(" ");
			if (!port_printed) {
				ccprintf("P%d  ", p);
				port_printed = 1;
			} else {
				ccprintf("    ");
			}
			ccprintf("%-10s  %4d  %5dmA %5dmV\n",
				 charge_supplier_name[s], supplier_priority[s],
				 available_charge[s][p].current,
				 available_charge[s][p].voltage);
		}
	}
	ccprintf("\n");
	ccprintf("  %s safe mode\n", left_safe_mode ? "Left" : "In");
	ccprintf("  Override port = P%d\n", charge_manager_get_override());
	ccprintf("\n");
	return 0;
}
DECLARE_CONSOLE_COMMAND(chgsup, charge_supplier_info, NULL,
			"print chg supplier info");
#endif

__overridable int board_charge_port_is_sink(int port)
{
	return 1;
}

__overridable int board_charge_port_is_connected(int port)
{
	return 1;
}

__overridable void
board_fill_source_power_info(int port, struct ec_response_usb_pd_power_info *r)
{
	r->meas.voltage_now = 0;
	r->meas.voltage_max = 0;
	r->meas.current_max = 0;
	r->meas.current_lim = 0;
	r->max_power = 0;
}

__overridable void board_set_charge_limit(int port, int supplier, int charge_ma,
					  int max_ma, int charge_mv)
{
#if defined(CONFIG_CHARGER) && defined(CONFIG_BATTERY)
	charge_set_input_current_limit(charge_ma, charge_mv);
#endif
}
