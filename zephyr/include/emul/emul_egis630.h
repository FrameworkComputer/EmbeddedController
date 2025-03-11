/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_INCLUDE_EMUL_EMUL_EGIS630_H_
#define ZEPHYR_INCLUDE_EMUL_EMUL_EGIS630_H_

/**
 * Stop SPI transactions
 *
 * @param target The target emulator
 */
void egis630_stop_spi(const struct emul *target);

#endif /* ZEPHYR_INCLUDE_EMUL_EMUL_EGIS630_H_ */
