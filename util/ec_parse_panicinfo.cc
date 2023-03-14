/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Standalone utility to parse EC panicinfo.
 */

#include "compile_time_macros.h"

#include <stdint.h>
#include <stdio.h>

#include <libec/ec_panicinfo.h>

int main(int argc, char *argv[])
{
	/*
	 * panic_data size could change with time, as new architecture are
	 * added (or, less likely, removed).
	 */
	const size_t max_size = 4096;

	BUILD_ASSERT(max_size > sizeof(struct panic_data) * 2);

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

	auto data = ec::GetPanicInput(4096);
	if (!data.has_value()) {
		fprintf(stderr, "%s", data.error().c_str());
		return 1;
	}

	auto result = ec::ParsePanicInfo(data.value());

	if (!result.has_value()) {
		fprintf(stderr, "%s", result.error().c_str());
		return 1;
	}
	printf("%s", result.value().c_str());

	return 0;
}
