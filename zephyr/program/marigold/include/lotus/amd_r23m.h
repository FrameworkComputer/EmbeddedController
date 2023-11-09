/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPU AMD R23M configuration */

#ifndef __CROS_EC_AMD_R23M_H
#define __CROS_EC_AMD_R23M_H

#include "i2c.h"

/* GPU features */
#define AMD_R23M_LOCAL 0

struct amdr23m_sensor_t {
	int i2c_port;
	int i2c_addr_flags;
};

extern const struct amdr23m_sensor_t amdr23m_sensors[];

/*
 * get GPU temperature value and move to *tem_ptr
 * One second trigger ,Use I2C read GPU's Die temperature.
 */

int get_temp_amd_R23M(int idx, int *temp_ptr);

#ifdef CONFIG_ZEPHYR
void amdr23m_update_temperature(int idx);
#endif

int amdr23m_get_val_k(int idx, int *temp);

#endif /* __CROS_EC_AMD_R23M_H */
