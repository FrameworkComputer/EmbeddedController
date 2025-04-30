/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

 #ifndef __CROS_EC_POWER_MONITOR_H
#define __CROS_EC_POWER_MONITOR_H

#define INA236_IDX_PSU_12V INA236_INDEX_ADD_PIN_GND
#define INA236_IDX_PSU_5V  INA236_INDEX_ADD_PIN_VS

/**
 * Return the alert flag
 *
 * @return true when the EC detects the alert
 */
bool power_monitor_get_5vsb_alert(void);

#endif /* __CROS_EC_POWER_MONITOR_H */
