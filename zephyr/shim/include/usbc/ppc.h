/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHROME_USBC_PPC_H
#define ZEPHYR_CHROME_USBC_PPC_H

#include <device.h>
#include <devicetree.h>

#define PPC_USBC_PORT(id) DT_REG_ADDR(DT_PARENT(id))

#endif /* ZEPHYR_CHROME_USBC_PPC_H */
