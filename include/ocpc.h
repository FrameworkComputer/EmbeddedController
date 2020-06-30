/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * OCPC - One Charger IC per Type-C
 */

#ifndef __CROS_EC_OCPC_H_
#define __CROS_EC_OCPC_H_

#define OCPC_UNINIT 0xdededede

struct ocpc_data {
	/* Index into chg_chips[] table for the charger IC that is switching. */
	int active_chg_chip;

	int combined_rsys_rbatt_mo; /* System resistance b/w output and Vbatt */

	/* ADC values */
	int primary_vbus_mv; /* VBUS measured by the primary charger IC */
	int primary_ibus_ma; /* IBUS measrued by the primary charger IC */
	int secondary_vbus_mv; /* VBUS measured by the secondary charger IC */
	int secondary_ibus_ma; /* IBUS measure by the secondary charger IC */

	/* PID values */
	int last_error;
	int integral;
	int last_vsys;
};

/** Set the VSYS target for the secondary charger IC.
 *
 * @param curr: Pointer to desired_input_current
 * @param ocpc: Pointer to OCPC data
 * @param voltage_mv: The desired voltage
 * @param current_ma: The desired current
 * @return EC_SUCCESS on success, error otherwise.
 */
int ocpc_config_secondary_charger(int *desired_input_current,
				  struct ocpc_data *ocpc,
				  int voltage_mv, int current_ma);

/** Get the runtime data from the various ADCs.
 *
 * @param ocpc: Pointer to OCPC data
 */
void ocpc_get_adcs(struct ocpc_data *ocpc);

/* Set the PID constants for the charging loop */
__overridable void ocpc_get_pid_constants(int *kp, int *kp_div,
					  int *ki, int *ki_div,
					  int *kd, int *kd_div);

#endif /* __CROS_EC_OCPC_H */
