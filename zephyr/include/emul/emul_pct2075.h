/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_PCT2075_H
#define EMUL_PCT2075_H

#include <zephyr/drivers/emul.h>
#include "emul/emul_common_i2c.h"

#define PCT2075_REG_NUMBER 5

struct pct2075_data {
	struct i2c_common_emul_data common;
	uint16_t regs[PCT2075_REG_NUMBER];
};

/**
 * @brief Set the temperature measurement for the sensor.
 *
 * @param emul Pointer to emulator
 * @param mk Temperature to set in mili-kalvin. The temperature
 * should me in range of 328150 to 400150, with 150 resolution.
 *
 * @return 0 on success
 * @return negative on error
 */
int pct2075_emul_set_temp(const struct emul *emul, int mk);

#endif
