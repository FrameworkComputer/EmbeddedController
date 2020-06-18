/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock for the TCPM interface */

#include "common.h"
#include "console.h"
#include "memory.h"
#include "mock/tcpm_mock.h"

int tcpm_dequeue_message(int port, uint32_t *payload, int *header)
{
	/* TODO flesh out the mock*/
	return 0;
}

int tcpm_has_pending_message(int port)
{
	/* TODO flesh out the mock*/
	return 0;
}
