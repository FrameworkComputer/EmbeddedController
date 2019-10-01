/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fpsensor_state.h"
#include "panic.h"

/*
 * These are built when we don't have the private driver files.
 * The definitions are intentionally kept separate from the mock
 * implementations so that we don't accidentally compile mocks into a release.
 */

#if !defined(HAVE_FP_PRIVATE_DRIVER)

int fp_sensor_init(void)
{
	panic("fp_sensor_init stub should never be called\n");
	return EC_ERROR_UNKNOWN;
}

int fp_sensor_deinit(void)
{
	panic("fp_sensor_deinit stub should never be called\n");
	return EC_ERROR_UNKNOWN;
}

#endif  /* !HAVE_FP_PRIVATE_DRIVER */
