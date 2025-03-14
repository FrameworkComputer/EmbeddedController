/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPU temp configuration */

#ifndef __CROS_EC_GPU_TEMP_H
#define __CROS_EC_GPU_TEMP_H

void gpu_update_temperature(int idx);

int gpu_get_val_k(int idx, int *temp);

#endif /* __CROS_EC_GPU_TEMP_H */
