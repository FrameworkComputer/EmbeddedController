/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define PD_OPERATING_POWER_MW	15000
#define PD_MAX_POWER_MW		65000
#define PD_MAX_CURRENT_MA	3250
#define PD_MAX_VOLTAGE_MV	20000

/* Stubs required by the shared code */
#define GPIO_PIN(port, index) (GPIO_##port, BIT(index))
#define GPIO_PIN_MASK(p, m) .port = GPIO_##p, .mask = (m)
