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

/*
 * Register functions to start and stop TPM communications layer. The
 * communications layer should be kept down while TPM is being reset.
 */
typedef void (*interface_control_func)(void);
void tpm_register_interface(interface_control_func interface_start,
			    interface_control_func interface_stop);

/*
 * This requests the TPM task to reset itself.
 *
 * If wait_until_done is false, it returns EC_SUCCESS immediately. Otherwise it
 * returns EC_SUCCESS after the reset has completed, or an error code on
 * failure.
 *
 * If wipe_nvmem_first is true, the caller is expected to keep the rest of the
 * system in reset until TPM wipeout is completed.
 */
int tpm_reset_request(int wait_until_done, int wipe_nvmem_first);

/*
 * Tell the TPM task to re-enable nvmem commits.
 *
 * NOTE: This function is NOT to be used freely, but only meant to be used in
 * exceptional cases such as unlocking the console following a TPM wipe.
 */
void tpm_reinstate_nvmem_commits(void);

/*
 * To be called by functions running on the TPM task context. Returns
 * EC_SUCCESS on successful reset.
 */
int tpm_sync_reset(int wipe_first);

/*
 * It shuts down the tpm interface, until next tpm reset event.
 */
void tpm_stop(void);

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
 * This function allows to process a TPM command coming from elsewhere, not
 * from the communications interface.
 *
 * A common use case would be making cryptographic calculation on task
 * contexts where stack the size is not large enough, for instance console
 * commands. This function will block to let the TPM task a chance to run to
 * execute the command and return the result in the same buffer.
 *
 * @param tpmh pointer to a buffer containing a marshalled TPM command, if it
 *             arrived over the communications channel. One of the header
 *             fields defines the command size.
 *
 * @param buffer_size the size of the buffer pointed to by tpmh - tells the
 *             TPM task how much room there is to store the response.
 *
 * Command execution result is reported in the response body.
 *
 * The extension command handler will consider all these commands to come from
 * the USB interface, since the only current users for this are console
 * commands.
 */
void tpm_alt_extension(struct tpm_cmd_header *tpmh, size_t buffer_size);

/*
 * The only TPM2 command we care about on the driver level, see
 * crosbug.com/p/55667 for detals.
 */
#define TPM2_PCR_Read		0x0000017e
#define TPM2_Startup		0x00000144

/* TPM mode */
enum tpm_modes {
	TPM_MODE_ENABLED_TENTATIVE = 0,
	TPM_MODE_ENABLED = 1,
	TPM_MODE_DISABLED = 2,
	TPM_MODE_MAX,
};

/*
 * This function returns the current TPM_MODE value.
 */
enum tpm_modes get_tpm_mode(void);

#endif	/* __CROS_EC_TPM_REGISTERS_H */
