/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PECI based cpu power governer
 */

#ifndef __CROS_EC_CPU_POWER_H
#define __CROS_EC_CPU_POWER_H

#define SB_RMI_WRITE_SUSTAINED_POWER_LIMIT_CMD  0x30
#define SB_RMI_WRITE_FAST_PPT_LIMIT_CMD 0x31
#define SB_RMI_WRITE_SLOW_PPT_LIMIT_CMD 0x32
#define SB_RMI_WRITE_SLOW_PPT_LIMIT_CMD 0x32
#define SB_RMI_WRITE_APU_ONLY_SPPT_CMD 0x3B
#define SB_RMI_WRITE_P3T_LIMIT_CMD 0x3C

enum power_limit_type {
	TYPE_SPL = 0,
	TYPE_SPPT,
	TYPE_FPPT,
	TYPE_P3T,
#ifdef CONFIG_BOARD_LOTUS
	TYPE_APU_ONLY_SPPT,
#endif
	TYPE_COUNT,
};

enum power_limit_function {
	FUNCTION_DEFAULT = 0,
	FUNCTION_SLIDER = 0,
	FUNCTION_THERMAL_PMF,
	FUNCTION_SAFETY,
	FUNCTION_POWER,
	FUNCTION_THERMAL,
	FUNCTION_COUNT,
};

enum power_safety_level {
	LEVEL_NORMAL = 0,
	LEVEL_STOP_CHARGE,
	LEVEL_TUNE_PLS,
	LEVEL_DISABLE_GPU,
	LEVEL_PROCHOT,
	LEVEL_TYPEC_1_5A,
	LEVEL_COUNT,
};

struct power_limit_details {
	int mwatt[TYPE_COUNT];
} __ec_align1;

#define BATTERY_55mW 55000
#define BATTERY_61mW 61000
/* ROP: rest of platform */
#define POWER_ROP 20000
#define POWER_PORT_COST 5000

extern struct power_limit_details power_limit[FUNCTION_COUNT];
extern int target_func[TYPE_COUNT];
extern bool manual_ctl;
extern bool safety_pwr_logging;
extern int mode_ctl;
extern uint8_t func_ctl;
extern int my_test_current;

void update_apu_ready(int status);
int get_apu_ready(void);

void update_soc_power_limit(bool force_update, bool force_no_adapter);
int set_pl_limits(uint32_t spl, uint32_t fppt, uint32_t sppt, uint32_t p3t);

#ifdef CONFIG_BOARD_LOTUS
int update_apu_only_sppt_limit(uint32_t mwatt);
void update_pmf_events(uint8_t pd_event, int enable);
#endif

extern bool thermal_warn_trigger(void);
extern int cypd_get_ac_power(void);

#endif	/* __CROS_EC_CPU_POWER_H */
