/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_GPU_H__
#define __BOARD_GPU_H__

bool gpu_present(void);

bool gpu_module_fault(void);

void set_host_dp_ready(int ready);

#endif /* __BOARD_GPU_H__ */
