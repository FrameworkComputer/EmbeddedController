/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PECI based cpu power governer
 */

#ifndef __CROS_EC_CPU_POWER_H
#define __CROS_EC_CPU_POWER_H

#ifdef CONFIG_CHIPSET_AMD
#include "amd_cpu_power_interface.h"
#elif CONFIG_CHIPSET_INTEL
#include "intel_cpu_power_interface.h"
#endif

void update_soc_power_limit(bool force_update, bool force_no_adapter);

#endif	/* __CROS_EC_CPU_POWER_H */
