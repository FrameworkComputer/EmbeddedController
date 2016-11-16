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

#include "common.h"

/* The SPI master is writing data into a TPM register. */
void tpm_register_put(uint32_t regaddr,
		      const uint8_t *data, uint32_t data_size);

/* The SPI master is reading data from a TPM register. */
void tpm_register_get(uint32_t regaddr, uint8_t *dest, uint32_t data_size);

/* Get the current value of the burst size field of the status register. */
size_t tpm_get_burst_size(void);

/* Register a function to restart TPM communications layer. */
typedef void (*interface_restart_func)(void);
void tpm_register_interface(interface_restart_func interface_restart);

/*
 * This requests the TPM task to reset itself.
 *
 * If wait_until_done is false, it returns EC_SUCCESS immediately. Otherwise it
 * returns EC_SUCCESS after the reset has completed, or an error code on
 * failure.
 *
 * If wipe_nvmem_first is true, the EC and AP will be forced off and TPM memory
 * will be erased before the TPM task is reset.
 */
int tpm_reset(int wait_until_done, int wipe_nvmem_first);

/*
 * Return true if the TPM is being reset. Usually this helps to avoid
 * unnecessary extra reset early at startup time, when TPM could be busy
 * installing endorsement certificates.
 */
int tpm_is_resetting(void);

/*
 * This structure describes the header of all commands and responses sent and
 * received over TPM FIFO.
 *
 * Note that all fields are stored in the network (big endian) byte order.
 */

struct tpm_cmd_header {
	uint16_t tag;
	uint32_t size;
	uint32_t command_code;
	uint16_t subcommand_code;  /* Not a standard field. */
} __packed;

/*
 * The only TPM2 command we care about on the driver level, see
 * crosbug.com/p/55667 for detals.
 */
#define TPM2_PCR_Read		0x0000017e

#endif	/* __CROS_EC_TPM_REGISTERS_H */
