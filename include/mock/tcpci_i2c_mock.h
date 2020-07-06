/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#define MOCK_TCPCI_I2C_ADDR_FLAGS 0x99

void mock_tcpci_reset(void);

void mock_tcpci_set_reg(int reg, uint16_t value);

uint16_t mock_tcpci_get_reg(int reg_offset);
