/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __MOCK_CLOCK_MOCK_H
#define __MOCK_CLOCK_MOCK_H

#include "clock.h"

#ifdef __cplusplus
extern "C" {
#endif

void clock_enable_module(enum module_id module, int enable);

int get_mock_fast_cpu_status(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOCK_CLOCK_MOCK_H */
