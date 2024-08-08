/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_INTELRVP_SRC_FAKES_H
#define ZEPHYR_TEST_INTELRVP_SRC_FAKES_H

#include "ccgxxf.h"
#include "ioexpander.h"
#include "it8801.h"
#include "lid_switch.h"
#include "nct38xx.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DECLARE_FAKE_VOID_FUNC(nct38xx_reset_notify, int);
DECLARE_FAKE_VALUE_FUNC(int, ccgxxf_reset, int);
DECLARE_FAKE_VOID_FUNC(lid_interrupt, enum gpio_signal);
DECLARE_FAKE_VALUE_FUNC(int, ioex_init, int);
DECLARE_FAKE_VOID_FUNC(io_expander_it8801_interrupt, enum gpio_signal);

#endif /* ZEPHYR_TEST_INTELRVP_SRC_FAKES_H */
