/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_NX20_348X_TEST_SHARED_H_
#define ZEPHYR_TEST_DRIVERS_NX20_348X_TEST_SHARED_H_

#include "emul/emul_nx20p348x.h"

#define TEST_PORT USBC_PORT_C0

struct nx20p348x_driver_fixture {
	const struct emul *nx20p348x_emul;
};

#endif /* ZEPHYR_TEST_DRIVERS_NX20_348X_TEST_SHARED_H_ */
