/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DPS__H
#define __CROS_EC_DPS__H

#include "common.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DPS_FLAG_DISABLED BIT(0)
#define DPS_FLAG_NO_SRCCAP BIT(1)
#define DPS_FLAG_WAITING BIT(2)
#define DPS_FLAG_SAMPLED BIT(3)
#define DPS_FLAG_NEED_MORE_PWR BIT(4)
#define DPS_FLAG_NO_BATTERY BIT(5)

/* Dynamic PDO Selection config. */
struct dps_config_t {
	/* (0, 100) coeff for transition to a lower power PDO*/
	uint32_t k_less_pwr;
	/* (0, 100) coeff for transition to a higher power PDO*/
	uint32_t k_more_pwr;
	/* Number for how many the same consecutive sample to transist */
	uint32_t k_sample;
	/* Number for moving average window for the power and the current. */
	uint32_t k_window;
	/* Power stabilized time after a new contract in us */
	uint32_t t_stable;
	/* Next power evaluation time interval in us */
	uint32_t t_check;
	/*
	 * If the current voltage is more efficient than the previous voltage
	 *
	 * @param curr_mv: current PDO voltage
	 * @param prev_mv: previous PDO voltage
	 * @param batt_mv: battery desired voltage
	 * @param batt_mw: current battery power
	 * @param input_mw: current adapter input power
	 * @return true is curr_mv is more efficient otherwise false
	 */
	bool (*is_more_efficient)(int curr_mv, int prev_mv, int batt_mv,
				  int batt_mw, int input_mw);
};

/*
 * Get voltage in the current system load
 *
 * @return a voltage(mV) that the adapter supports to charge at the given port.
 */
int dps_get_dynamic_voltage(void);

/*
 * Get DPS charge port
 *
 * @return the DPS charge port, or CHARGE_PORT_NONE if unavailable.
 */
int dps_get_charge_port(void);

/*
 * Check if DPS is enabled.
 *
 * @return true if enabled, false otherwise.
 */
bool dps_is_enabled(void);

/*
 * Update DPS stablized timeout
 *
 * This is called at the exit of PE_SNK_TRANSITION_SINK
 *
 * @param port: the port for timer reset.
 */
void dps_update_stabilized_time(int port);

#ifdef TEST_BUILD
__test_only void dps_enable(bool en);
__test_only int dps_init(void);
__test_only struct dps_config_t *dps_get_config(void);
__test_only bool dps_is_fake_enabled(void);
__test_only int dps_get_fake_mv(void);
__test_only int dps_get_fake_ma(void);
__test_only int *dps_get_debug_level(void);
__test_only int dps_get_flag(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DPS__H */
