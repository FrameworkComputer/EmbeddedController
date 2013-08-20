/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Per-test config flags */

#ifndef __CROS_EC_TEST_CONFIG_H
#define __CROS_EC_TEST_CONFIG_H

#ifdef TEST_kb_8042
#undef CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_KEYBOARD_PROTOCOL_8042
#endif

#ifdef TEST_sbs_charging
#define CONFIG_BATTERY_MOCK
#define CONFIG_BATTERY_SMART
#define CONFIG_CHARGER
#define CONFIG_CHARGER_INPUT_CURRENT 4032
#define CONFIG_CHARGER_DISCHARGE_ON_AC
int board_discharge_on_ac(int enabled);
#define I2C_PORT_HOST 1
#define I2C_PORT_BATTERY 1
#define I2C_PORT_CHARGER 1
#endif

#ifdef TEST_adapter
#define CONFIG_EXTPOWER_FALCO
#endif

#ifdef TEST_thermal
#define CONFIG_TEMP_SENSOR
#endif

#endif  /* __CROS_EC_TEST_CONFIG_H */
