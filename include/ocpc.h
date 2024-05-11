/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * OCPC - One Charger IC per Type-C
 */

#ifndef __CROS_EC_OCPC_H_
#define __CROS_EC_OCPC_H_

#include "battery.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCPC_UNINIT 0xdededede

struct ocpc_data {
	/* Index into chg_chips[] table for the charger IC that is switching. */
	int active_chg_chip;

	int combined_rsys_rbatt_mo; /* System resistance b/w output and Vbatt */
	int rsys_mo; /* System resistance b/w output and VSYS node */
	int rbatt_mo; /* Resistance between VSYS node and battery */

	/* ADC values */
	int primary_vbus_mv; /* VBUS measured by the primary charger IC */
	int primary_ibus_ma; /* IBUS measrued by the primary charger IC */
	int secondary_vbus_mv; /* VBUS measured by the secondary charger IC */
	int secondary_ibus_ma; /* IBUS measure by the secondary charger IC */
	int vsys_aux_mv; /* VSYS output measured by aux charger IC */
	int vsys_mv; /* VSYS measured by main charger IC */
	int isys_ma; /* Egress current measured by aux charger IC */

	/* PID values */
	int last_error;
	int integral;
	int last_vsys;
#ifdef HAS_TASK_PD_C1
	uint32_t chg_flags[CONFIG_USB_PD_PORT_MAX_COUNT];
#endif /* HAS_TASK_PD_C1 */
};

#define OCPC_NO_ISYS_MEAS_CAP BIT(0)

/** Set the VSYS target for the secondary charger IC.
 *
 * @param desired_charger_input_current: Pointer to desired_input_current
 * @param ocpc: Pointer to OCPC data
 * @param desired_batt_current_ma: The desired voltage
 * @param desired_batt_voltage_mv: The desired current
 * @return EC_SUCCESS on success, error otherwise.
 */
int ocpc_config_secondary_charger(int *desired_charger_input_current,
				  struct ocpc_data *ocpc,
				  int desired_batt_voltage_mv,
				  int desired_batt_current_ma);

/** Get the runtime data from the various ADCs.
 *
 * @param ocpc: Pointer to OCPC data
 */
void ocpc_get_adcs(struct ocpc_data *ocpc);

/* Set the PID constants for the charging loop */
__override_proto void ocpc_get_pid_constants(int *kp, int *kp_div, int *ki,
					     int *ki_div, int *kd, int *kd_div);

/*
 ** Set up some initial values for the OCPC data structure.  This will call off
 * to board_ocpc_init() such that boards can set up any charger flags if needed.
 *
 * @param ocpc: Pointer to OCPC data
 */
void ocpc_init(struct ocpc_data *ocpc);

/**
 * Reset the OCPC module.  This also sets the initial VSYS target to the current
 * battery voltage
 *
 * @param ocpc: Pointer to OCPC data
 */
void ocpc_reset(struct ocpc_data *ocpc);

/**
 * Board specific OCPC data structure initialization.  This can be used to set
 * up and charger flags.  The default implementation does nothing.
 *
 * @param ocpc: Pointer to OCPC data
 */
__override_proto void board_ocpc_init(struct ocpc_data *ocpc);

#ifdef TEST_BUILD
/**
 * @brief Force a reload of PID constants by calling ocpc_get_pid_constants().
 */
void ocpc_set_pid_constants(void);

/**
 * @brief Return the value of viz_output
 *
 * @return int
 */
int test_ocpc_get_viz_output(void);

/**
 * @brief Return the value of debug_output
 *
 * @return int
 */
int test_ocpc_get_debug_output(void);

/**
 * @brief Reset state used to track resistance calculations.
 */
void test_ocpc_reset_resistance_state(void);

/**
 * @brief Calculate the system impedance
 *
 * @param ocpc OCPC data struct pointer
 * @param battery Battery params pointer
 * @return enum ec_error_list Success or error code
 */
enum ec_error_list ocpc_calc_resistances(struct ocpc_data *ocpc,
					 struct batt_params *battery);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_OCPC_H */
