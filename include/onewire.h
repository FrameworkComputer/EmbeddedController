/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* 1-wire interface for Chrome EC */

/*
 * Note that 1-wire communication is VERY latency-sensitive.  If these
 * functions are run at low priority, communication may be garbled.  However,
 * these functions are also slow enough (~1ms per call) that it's really not
 * desirable to put them at high priority.  So make sure you check the
 * confirmation code from the slave for any communication, and retry a few
 * times in case of failure.
 */

#ifndef __CROS_EC_ONEWIRE_H
#define __CROS_EC_ONEWIRE_H

#include "common.h"

/**
 * Reset the 1-wire bus.
 *
 * @return EC_SUCCESS, or non-zero if presence detect fails.
 */
int onewire_reset(void);

/**
 * Read a byte from the 1-wire bus.
 *
 * @return The byte value read.
 */
int onewire_read(void);

/**
 * Write a byte to the 1-wire bus.
 *
 * @param data		Byte to write
 */
void onewire_write(int data);

#endif  /* __CROS_EC_ONEWIRE_H */
