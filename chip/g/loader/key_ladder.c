/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "key_ladder.h"
#include "debug_printf.h"
#include "registers.h"
/* #include "setup.h" */

#include "dcrypto.h"

void key_ladder_step(uint32_t cert, const uint32_t *input)
{
	uint32_t flags;

	GREG32(KEYMGR, SHA_ITOP) = 0;  /* Clear status. */

	VERBOSE("Cert %2u: ", cert);

	GWRITE_FIELD(KEYMGR, SHA_USE_CERT, INDEX, cert);
	GWRITE_FIELD(KEYMGR, SHA_USE_CERT, ENABLE, 1);
	GWRITE_FIELD(KEYMGR, SHA_CFG_EN, INT_EN_DONE, 1);
	GWRITE_FIELD(KEYMGR, SHA_TRIG, TRIG_GO, 1);

	if (input) {
		int i;

		for (i = 0; i < 8; ++i)
			GREG32(KEYMGR, SHA_INPUT_FIFO) = *input++;

		GWRITE_FIELD(KEYMGR, SHA_TRIG, TRIG_STOP, 1);
	}

	while (!GREG32(KEYMGR, SHA_ITOP))
		;

	GREG32(KEYMGR, SHA_ITOP) = 0;  /* Clear status. */

	flags = GREG32(KEYMGR, HKEY_ERR_FLAGS);
	if (flags)
		debug_printf("Cert %2u: fail %x\n", cert, flags);
	else
		VERBOSE("flags %x\n", flags);
}
