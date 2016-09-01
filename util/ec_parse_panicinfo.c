/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Standalone utility to parse EC panicinfo.
 */

#include <stdint.h>
#include <stdio.h>
#include "ec_panicinfo.h"

int main(int argc, char *argv[])
{
	struct panic_data pdata;

	if (fread(&pdata, sizeof(pdata), 1, stdin) != 1) {
		fprintf(stderr, "Error reading panicinfo from stdin.\n");
		return 1;
	}

	return parse_panic_info(&pdata) ? 1 : 0;
}
