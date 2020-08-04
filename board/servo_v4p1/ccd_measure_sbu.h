/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CCD_MEASURE_SBU_H
#define __CROS_EC_CCD_MEASURE_SBU_H

/**
 * Enables or disables CCD for use with SuzyQ cable
 *
 * @param en    0 - Disable CCD
 *              1 - Enable CCD
 */
void ccd_enable(int enable);

/**
 * Triggers the detection of a SuzyQ cable every 100mS
 */
void start_ccd_meas_sbu_cycle(void);

#endif /* __CROS_EC_CCD_MEASURE_SBU_H */
