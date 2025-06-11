/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

 #ifndef __CROS_EC_POWER_MONITOR_H
#define __CROS_EC_POWER_MONITOR_H

#define INA236_IDX_PSU_12V INA236_INDEX_ADD_PIN_GND
#define INA236_IDX_PSU_5V  INA236_INDEX_ADD_PIN_VS

#define INA236_MONITOR_12V_CURRENT 30000 /* 30 A */
#define INA236_MONITOR_5V_UPPER_CURRENT_MA  1000
#define INA236_MONITOR_5V_LOWER_CURRENT_MA   700

/**
 * Return the alert flag
 *
 * @return true when the EC detects the alert
 */
bool power_monitor_get_5vsb_alert(void);

/**
 * Set the alert current
 *
 * @param idx the index of the ina236
 * @param current set the alert current
 */
void power_monitor_set_alert_current(int idx, int ma);

#endif /* __CROS_EC_POWER_MONITOR_H */
