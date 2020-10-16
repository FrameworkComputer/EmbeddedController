/* -*- mode:c -*-
 *
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * we need at least one GPIO definition because much of the code does not
 * handle the case of no GPIOs.
 */

GPIO_INT(FAKE_IRQ_00, PIN(3, 4), GPIO_INT_BOTH, fake_gpio_interrupt)

GPIO(FAKE_OUT_01, PIN(5, 6), GPIO_OUTPUT)
