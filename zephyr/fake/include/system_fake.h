/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_FAKE_SYSTEM_FAKE_H
#define ZEPHYR_FAKE_SYSTEM_FAKE_H

#include <setjmp.h>

#include "ec_commands.h"

/**
 * @brief Set the current image copy.
 */
void system_set_shrspi_image_copy(enum ec_image new_image_copy);

/**
 * @brief Set the fake environment
 */
void system_fake_setenv(jmp_buf *env);

#endif /* ZEPHYR_FAKE_SYSTEM_FAKE_H */
