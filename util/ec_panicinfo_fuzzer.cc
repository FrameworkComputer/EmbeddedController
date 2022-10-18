/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_panicinfo.h"

/* Fuzzing Build command:
 * $ clang++ ec_panicinfo_fuzzer.cc ec_panicinfo.cc -g -fsanitize=address,fuzzer
 *   -o ec_panicinfo_fuzzer
 *   -I../include/ -I../chip/host/ -I../board/host/ -I../fuzz -I../test
 *
 * Run Fuzzing:
 *  $ ./ec_panicinfo_fuzzer -runs=5000
 */

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, unsigned int size)
{
	parse_panic_info((const char *)data, size);

	return 0;
}
