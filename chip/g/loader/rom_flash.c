/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "debug_printf.h"
#include "setup.h"
#include "rom_flash.h"

static int _flash_error(void)
{
	int retval = GREG32(FLASH, FSH_ERROR);

	if (!retval)
		return 0;

	debug_printf("Register FLASH_FSH_ERROR is not zero (found %x).\n",
		retval);
	debug_printf("Will read again to verify FSH_ERROR was cleared ");
	debug_printf("and then continue...\n");

	retval = GREG32(FLASH, FSH_ERROR);
	if (retval) {
		debug_printf("ERROR: Read to FLASH_FSH_ERROR (%x) ");
		debug_printf("did not clear it\n", retval);
	}

	return retval;
}

/* Verify the flash controller is awake. */
static int _check_flash_is_awake(void)
{
	int retval;

	GREG32(FLASH, FSH_TRANS) = 0xFFFFFFFF;
	retval = GREG32(FLASH, FSH_TRANS);
	GREG32(FLASH, FSH_TRANS) =  0x0;

	if (retval == 0) {
		debug_printf("ERROR:FLASH Controller seems unresponsive. ");
		debug_printf("Did you make sure to run 'reseth'?\n");
		return E_FL_NOT_AWAKE;
	}

	return 0;
}

/* Send cmd to flash controller. */
static int _flash_cmd(uint32_t fidx, uint32_t cmd)
{
	int cnt, retval;

	/* Activate controller. */
	GREG32(FLASH, FSH_PE_EN) = FSH_OP_ENABLE;
	GREG32_ADDR(FLASH, FSH_PE_CONTROL0)[fidx] = cmd;

	/* wait on FSH_PE_EN (means the operation started) */
	cnt = 500;  /* TODO(mschilder): pick sane value. */

	do {
		retval = GREG32(FLASH, FSH_PE_EN);
	} while (retval && cnt--);

	if (retval) {
		debug_printf("ERROR: FLASH_FSH_PE_EN never went to 0, is ");
		debug_printf("0x%x after timeout\n", retval);
		return E_FL_TIMEOUT;
	}

	/*
	 * wait 100us before checking FSH_PE_CONTROL (means the operation
	 * ended)
	 */
	cnt = 1000000;
	do {
		retval = GREG32_ADDR(FLASH, FSH_PE_CONTROL0)[fidx];
	} while (retval && --cnt);

	if (retval) {
		debug_printf
		    ("ERROR: FLASH_FSH_PE_CONTROL%d is 0x%x after timeout\n",
		     fidx, retval);
		GREG32_ADDR(FLASH, FSH_PE_CONTROL0)[fidx] = 0;
		return E_FL_TIMEOUT;
	}

	return 0;
}

int flash_info_read(uint32_t offset, uint32_t *dst)
{
	int retval;

	/* Make sure flash controller is awake. */
	retval = _check_flash_is_awake();
	if (retval)
		return retval;

	GWRITE_FIELD(FLASH, FSH_TRANS, OFFSET, offset);
	GWRITE_FIELD(FLASH, FSH_TRANS, MAINB, 1);
	GWRITE_FIELD(FLASH, FSH_TRANS, SIZE, 1);

	retval = _flash_cmd(1, FSH_OP_READ);
	if (retval)
		return retval;

	if (_flash_error())
		return E_FL_ERROR;

	if (!retval)
		*dst = GREG32(FLASH, FSH_DOUT_VAL1);

	return retval;
}
