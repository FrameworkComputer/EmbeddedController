/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_DACS_H
#define __CROS_EC_DACS_H

#include <stdint.h>

enum dac_t {
	CC1_DAC = 1,
	CC2_DAC,
};

/*
 * Initialize the DACs
 */
void init_dacs(void);

/*
 * Enable/Disable one of the DACs
 *
 * @param dac DAC to enable or disable
 * @param en 0 to disable or 1 to enable
 */
void enable_dac(enum dac_t dac, uint8_t en);

/*
 * Write a value to the DAC
 *
 * @param dac DAC to write to
 * @param value to write to the DAC in mV. (0 to 5000mV)
 * @return EC_SUCCESS or EC_ERROR_ACCESS_DENIED on failure
 */
int write_dac(enum dac_t dac, uint16_t value);

#endif /* __CROS_EC_DACS_H */
