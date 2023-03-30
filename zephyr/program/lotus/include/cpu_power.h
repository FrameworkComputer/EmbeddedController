/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PECI based cpu power governer
 */

#ifndef __CROS_EC_CPU_POWER_H
#define __CROS_EC_CPU_POWER_H

#define SB_RMI_WRITE_SUSTAINED_POWER_LIMIT_CMD  0x30
#define SB_RMI_WRITE_FAST_PPT_LIMIT_CMD 0x31
#define SB_RMI_WRITE_SLOW_PPT_LIMIT_CMD 0x32

void update_soc_power_limit(bool force_update, bool force_no_adapter);

#endif	/* __CROS_EC_CPU_POWER_H */
