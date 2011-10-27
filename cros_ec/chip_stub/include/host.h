/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * host.h -- defines the API provided by chip_stub/host.c.
 */

#ifndef __CROS_EC_CHIP_STUB_INCLUDE_HOST_H
#define __CROS_EC_CHIP_STUB_INCLUDE_HOST_H

#include "cros_ec/include/ec_common.h"

int SimulateAcpiCommand(
    uint8_t command,
    uint8_t data,
    uint8_t *mailbox,
    uint8_t *output);

int SimulateI8042Command(
    uint8_t command,
    uint8_t data,
    uint8_t *output);

EcError PullI8042ScanCode(uint8_t *buf);

#endif  /* __CROS_EC_CHIP_STUB_INCLUDE_HOST_H */
