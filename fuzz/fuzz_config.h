/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fuzzer target config flags */

#ifndef __FUZZ_FUZZ_CONFIG_H
#define __FUZZ_FUZZ_CONFIG_H
#ifdef TEST_FUZZ

/* Disable hibernate: We never want to exit while fuzzing. */
#undef CONFIG_HIBERNATE

#ifdef TEST_HOST_COMMAND_FUZZ
#undef CONFIG_HOSTCMD_DEBUG_MODE

/* Defining this makes fuzzing slower, but exercises additional code paths. */
#define FUZZ_HOSTCMD_VERBOSE

#ifdef FUZZ_HOSTCMD_VERBOSE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_PARAMS
#else
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF
#endif /* ! FUZZ_HOSTCMD_VERBOSE */
#endif /* TEST_HOST_COMMAND_FUZZ */

#endif  /* TEST_FUZZ */
#endif  /* __TEST_TEST_CONFIG_H */
