/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPU NV GN22 configuration */

#ifndef __CROS_EC_NV_GN22_H
#define __CROS_EC_NV_GN22_H

/*
 * get GPU temperature value and move to *tem_ptr
 * One second trigger ,Use I2C read GPU temperature.
 */

int get_nv_gpu_temp(int idx, int *temp);

#endif /* __CROS_EC_NV_GN22_H */
