/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_POWER_MGMT_H
#define __CROS_EC_POWER_MGMT_H

/*
 * Method for getting current platform input power capabilities and configuring
 * alert interrupt when programmed power threshold is exceeded. This function
 * should be invoked every time the input power may change its value.
 */
void evaluate_input_power(void);

#endif
