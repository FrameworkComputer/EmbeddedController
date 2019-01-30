/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Filter tasklist in *.tasklist, depending on section (RO/RW), or
 * TEST/CTS build.
 */

#ifndef __CROS_EC_TASK_FILTER_H
#define __CROS_EC_TASK_FILTER_H

#ifdef SECTION_IS_RO
#define TASK_NOTEST_RO TASK_NOTEST
#define TASK_TEST_RO TASK_TEST
#define TASK_ALWAYS_RO TASK_ALWAYS
#define TASK_NOTEST_RW(...)
#define TASK_TEST_RW(...)
#define TASK_ALWAYS_RW(...)
#else /* SECTION_IS_RW */
#define TASK_NOTEST_RW TASK_NOTEST
#define TASK_TEST_RW TASK_TEST
#define TASK_ALWAYS_RW TASK_ALWAYS
#define TASK_NOTEST_RO(...)
#define TASK_TEST_RO(...)
#define TASK_ALWAYS_RO(...)
#endif

/* excludes non-base tasks for test build */
#ifdef TEST_BUILD
#define TASK_NOTEST(...)
#define TASK_TEST TASK
#else
#define TASK_NOTEST TASK
#define CONFIG_TEST_TASK_LIST
#endif

#ifndef CTS_MODULE
#define CONFIG_CTS_TASK_LIST
#endif

#define TASK_ALWAYS TASK

/* If included directly from Makefile, dump task list. */
#ifdef _MAKEFILE
#define TASK(n, ...) n
CONFIG_TASK_LIST CONFIG_TEST_TASK_LIST CONFIG_CTS_TASK_LIST
#endif


#endif /*  __CROS_EC_TASK_FILTER_H */
