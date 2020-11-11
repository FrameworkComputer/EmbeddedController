/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PECI_CHIP_H
#define __CROS_EC_PECI_CHIP_H

#include "common.h"

/* PECI paramters */


/*completion codes*/
#define COMP_CODE       0
#define CC_PASS_MASK    (0 << 7)
#define CC_FAIL_MASK    (1 << 7)
#define CC_PASSED       0x40        /* command passed */
#define CC_TIMED_OUT    0x80        /* command timed-out */
#define CC_BAD          0x90        /* unknown-invalid-illegal request */
#define CPU_ADDR        0x30

/* PECI Command Code */
enum peci_command_t {
	PECI_COMMAND_PING               = 0x00,
	PECI_COMMAND_GET_DIB            = 0xF7,
	PECI_COMMAND_GET_TEMP           = 0x01,
	PECI_COMMAND_RD_PKG_CFG         = 0xA1,
	PECI_COMMAND_WR_PKG_CFG         = 0xA5,
	PECI_COMMAND_RD_IAMSR           = 0xB1,
	PECI_COMMAND_RD_PCI_CFG         = 0x61,
	PECI_COMMAND_RD_PCI_CFG_LOCAL   = 0xE1,
	PECI_COMMAND_WR_PCI_CFG_LOCAL   = 0xE5,
	PECI_COMMAND_NONE               = 0xFF
};

void peci_protocol(uint8_t peci_addr, uint8_t cmd_code, uint8_t domain,
        const uint8_t *out, int out_size, uint8_t *in, int in_size);

#endif  /* __CROS_EC_PECI_CHIP_H */