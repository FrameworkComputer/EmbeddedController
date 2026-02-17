/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_RETAINED_MEM_BBRAM_H
#define __CROS_EC_RETAINED_MEM_BBRAM_H

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief		Read from BBRAM based on retained memory.
 *
 * @param dev		Retained memory device.
 * @param offset	Offset to read data from.
 * @param size		Size of data to read.
 * @param data		Read buffer.
 *
 * @return		0 on success else negative errno code.
 */
int retained_mem_bbram_read(const struct device *dev, size_t offset,
			    size_t size, uint8_t *data);

/**
 * @brief		Write to BBRAM based on retained memory.
 *
 * @param dev		Retained memory device.
 * @param offset	Offset to write data to.
 * @param size		Size of data to write.
 * @param data		Data to write.
 *
 * @return		0 on success else negative errno code.
 */
int retained_mem_bbram_write(const struct device *dev, size_t offset,
			     size_t size, const uint8_t *data);

/**
 * @brief		Initialize BBRAM based on retained memory device.
 *
 * @param dev		Retained memory device.
 *
 * @return		0 on success else negative errno code.
 */
int retained_mem_bbram_init(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_RETAINED_MEM_BBRAM_H */
