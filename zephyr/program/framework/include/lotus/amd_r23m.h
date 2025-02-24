/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPU AMD R23M configuration */

#ifndef __CROS_EC_AMD_R23M_H
#define __CROS_EC_AMD_R23M_H

/*
 * get GPU temperature value and move to *tem_ptr
 * One second trigger ,Use I2C read GPU's Die temperature.
 */

int get_amd_gpu_temp(int idx, int *temp);

#endif /* __CROS_EC_AMD_R23M_H */
