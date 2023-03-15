/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EMUL_AMD_FP6_USB_MUX_H
#define EMUL_AMD_FP6_USB_MUX_H

#include <zephyr/drivers/emul.h>

/**
 * Reset the emulator's registers to their power-on value.
 *
 * @param emul - AMD FP6 emulator data
 */
void amd_fp6_emul_reset_regs(const struct emul *emul);

/**
 * Set whether the crossbar is ready to process commands.  On a real
 * system, it is typically not ready for some time after powering on to S0.
 *
 * @param emul - AMD FP6 emulator data
 * @param ready - whether the xbar should report it's ready
 *		 (emulator init default is on)
 */
void amd_fp6_emul_set_xbar(const struct emul *emul, bool ready);

/**
 * Set how long a command will take to complete.  On a real system this can be
 * anywhere from 50-100ms and the datasheet defines it can take up to 250ms.
 *
 * @param emul - AMD FP6 emulator data
 * @param delay_ms - how long after a mux set to wait before reporting the
 *		     status of the set as complete.
 */
void amd_fp6_emul_set_delay(const struct emul *emul, int delay_ms);

#endif
