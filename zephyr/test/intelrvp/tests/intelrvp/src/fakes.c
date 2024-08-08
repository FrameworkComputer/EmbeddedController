/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fakes.h"

/* These macros do not export fake resets. Avoid using these fakes in test
 * sources */
DEFINE_FAKE_VOID_FUNC(nct38xx_reset_notify, int);
DEFINE_FAKE_VALUE_FUNC(int, ccgxxf_reset, int);
DEFINE_FAKE_VOID_FUNC(lid_interrupt, enum gpio_signal);
DEFINE_FAKE_VALUE_FUNC(int, ioex_init, int);
DEFINE_FAKE_VOID_FUNC(io_expander_it8801_interrupt, enum gpio_signal);
