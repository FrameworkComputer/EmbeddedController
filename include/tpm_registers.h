/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This defines the interface functions for TPM SPI Hardware Protocol. The SPI
 * master reads or writes between 1 and 64 bytes to a register designated by a
 * 24-bit address. There is no provision for error reporting at this level.
 */

#ifndef __CROS_EC_TPM_REGISTERS_H
#define __CROS_EC_TPM_REGISTERS_H

#include <stdint.h>

/* The SPI master is writing data into a TPM register. */
void tpm_register_put(uint32_t regaddr,
		      const uint8_t *data, uint32_t data_size);

/* The SPI master is reading data from a TPM register. */
void tpm_register_get(uint32_t regaddr, uint8_t *dest, uint32_t data_size);

/* Enable SPS TPM driver. */
void sps_tpm_enable(void);

#endif	/* __CROS_EC_TPM_REGISTERS_H */
