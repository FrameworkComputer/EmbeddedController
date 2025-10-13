/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_QCOM_H_
#define __CROS_EC_POWER_QCOM_H_

#if defined(CONFIG_CHIPSET_SC7180) || defined(CONFIG_CHIPSET_SC7280)
enum power_signal {
	SC7X80_AP_RST_ASSERTED = 0,
	SC7X80_PS_HOLD,
	SC7X80_POWER_GOOD,
	SC7X80_AP_SUSPEND,
#ifdef CONFIG_CHIPSET_SC7180
	SC7X80_WARM_RESET,
	SC7X80_DEPRECATED_AP_RST_REQ,
#endif
	POWER_SIGNAL_COUNT,
};
#endif

#if defined(CONFIG_CHIPSET_QC_EXP)
enum power_signal {
	QC_EXP_AP_RST_ASSERTED = 0,
	QC_EXP_PS_HOLD,
	QC_EXP_POWER_GOOD,
	QC_EXP_AP_SUSPEND,
	POWER_SIGNAL_COUNT,
};
#endif

/* Swithcap functions */
void board_set_switchcap_power(int enable);
int board_is_switchcap_enabled(void);
int board_is_switchcap_power_good(void);

#if defined(CONFIG_PLATFORM_EC_PMIC_PASSTHRU_POWER_SIGNALS)
void passthru_lid_open_to_pmic(void);
void passthru_ac_on_to_pmic(void);
void reset_all_passthru_pmic_signal(void);
#endif

#endif /* __CROS_EC_POWER_QCOM_H_ */
