/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_TEST_TPM_TEST_FTDI_SPI_TPM_H
#define __EC_TEST_TPM_TEST_FTDI_SPI_TPM_H

#include "mpsse.h"

/*
 * This structure allows to convert string representation between C and
 * Python.
 */
struct swig_string_data {
	int size;
	uint8_t *data;
};

int FtdiSpiInit(uint32_t freq, int enable_debug);
void FtdiStop(void);
struct swig_string_data FtdiSendCommandAndWait(char *tpm_command,
					       int command_size);

#endif				/* ! __EC_TEST_TPM_TEST_FTDI_SPI_TPM_H */
