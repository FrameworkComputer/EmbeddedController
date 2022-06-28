/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __ZEPHYR_GPIO_MAP_H
#define __ZEPHYR_GPIO_MAP_H

/* GPIO_ENTERING_RW implemented by GPIO emulator on native_posix */
#ifndef CONFIG_BOARD_NATIVE_POSIX
#define GPIO_ENTERING_RW GPIO_UNIMPLEMENTED
#endif

#endif /* __ZEPHYR_GPIO_MAP_H */
