/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Test the STATIC_IF and STATIC_IF_NOT macros fail on unexpected
 * input.
 */

#include "common.h"
#include "test_util.h"

#define CONFIG_FOO TEST_VALUE

/*
 * At compiler invocation, define TEST_MACRO to STATIC_IF or
 * STATIC_IF_NOT.
 */
#ifndef TEST_MACRO
#error "This error should not be seen in the compiler output!"
#endif

/* This is intended to cause a compilation error. */
TEST_MACRO(CONFIG_FOO) __maybe_unused int foo;

void run_test(int argc, char **argv)
{
	test_reset();

	/* Nothing to do, observe compilation error */

	test_print_result();
}
