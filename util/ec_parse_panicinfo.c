/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Standalone utility to parse EC panicinfo.
 */

#include <stdint.h>
#include <stdio.h>
#include "compile_time_macros.h"
#include "ec_panicinfo.h"

int main(int argc, char *argv[])
{
	/*
	 * panic_data size could change with time, as new architecture are
	 * added (or, less likely, removed).
	 */
	char pdata[4096];
	size_t size = 0;
	size_t read;

	BUILD_ASSERT(sizeof(pdata) > sizeof(struct panic_data)*2);

	/*
	 * Provide a minimal help message.
	 */
	if (argc > 1) {
		printf("Usage: cat <PANIC_BLOB_PATH> | ec_parse_panicinfo\n");
		printf("Print the plain text panic info from a raw EC panic "
		       "data blob.\n\n");
		printf("Example:\n");
		printf("ec_parse_panicinfo "
		       "</sys/kernel/debug/cros_ec/panicinfo\n");
		return 1;
	}

	while (1) {
		read = fread(&pdata[size], 1, sizeof(pdata)-size, stdin);
		if (read < 0) {
			fprintf(stderr, "Cannot read panicinfo from stdin.\n");
			return 1;
		}
		if (read == 0)
			break;

		size += read;
		if (size == sizeof(pdata)) {
			fprintf(stderr, "Too much panicinfo data in stdin.\n");
			return 1;
		}
	}

	return parse_panic_info(pdata, size) ? 1 : 0;
}
