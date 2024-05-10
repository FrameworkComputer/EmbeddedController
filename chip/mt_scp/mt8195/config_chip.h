/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_MT8195_H
#define __CROS_EC_CONFIG_CHIP_MT8195_H

#define CONFIG_CHIP_MEMORY_REGIONS

#define CONFIG_PANIC_CONSOLE_OUTPUT

/* Add some space (0x100) before panic for jump data */
#define CONFIG_PANIC_BASE_OFFSET 0x100 /* reserved for jump data */
#define CONFIG_PANIC_DATA_BASE \
	(CONFIG_PANIC_DRAM_BASE + CONFIG_PANIC_BASE_OFFSET)

#endif /* __CROS_EC_CONFIG_CHIP_MT8195_H */
