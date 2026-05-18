/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/minmax.h>

#ifndef CONFIG_ZTEST
BUILD_ASSERT(CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY >
		     CONFIG_RETAINED_MEM_INIT_PRIORITY,
	     "CONFIG_PLATFORM_EC_SYSTEM_PRE_INIT_PRIORITY must be higher than "
	     "CONFIG_RETAINED_MEM_INIT_PRIORITY");
#endif /* CONFIG_ZTEST */

#define CRC_SIZE sizeof(uint16_t)
#define CRC_STARTING_SEED 0xAB12

bool retained_mem_bbram_inited = false;

static int retained_mem_bbram_crc(const struct device *dev, uint16_t *crc)
{
	ssize_t mem_size = retained_mem_size(dev);
#ifdef CONFIG_DCACHE_LINE_SIZE
	const size_t chunk_size = CONFIG_DCACHE_LINE_SIZE;
#else
	const size_t chunk_size = 32;
#endif
	uint8_t buf[chunk_size];
	ssize_t read_offset = 0;

	/* Handle too small memory and retained_mem_size error. */
	if (mem_size < CRC_SIZE) {
		return -EINVAL;
	}
	/* Last bytes are reserved for CRC value. */
	mem_size -= CRC_SIZE;

	/* Use starting seed, to always recognize uninitialize memory. */
	*crc = CRC_STARTING_SEED;
	while (mem_size > read_offset) {
		int ret;
		size_t read_size = min(chunk_size, mem_size - read_offset);

		ret = retained_mem_read(dev, read_offset, buf, read_size);
		if (ret < 0) {
			return ret;
		}
		*crc = crc16_itu_t(*crc, buf, read_size);
		read_offset += read_size;
	}

	return 0;
}

static int retained_mem_bbram_crc_update(const struct device *dev)
{
	int ret;
	uint16_t crc;
	const ssize_t mem_size = retained_mem_size(dev);

	/* Handle too small memory and retained_mem_size error. */
	if (mem_size < CRC_SIZE) {
		return -EINVAL;
	}

	ret = retained_mem_bbram_crc(dev, &crc);
	if (ret) {
		return ret;
	}
	ret = retained_mem_write(dev, mem_size - CRC_SIZE, (uint8_t *)&crc,
				 CRC_SIZE);

	return ret;
}

int retained_mem_bbram_read(const struct device *dev, size_t offset,
			    size_t size, uint8_t *data)
{
	if (!retained_mem_bbram_inited) {
		return -ENOTSUP;
	}
	return retained_mem_read(dev, offset, data, size);
}

int retained_mem_bbram_write(const struct device *dev, size_t offset,
			     size_t size, const uint8_t *data)
{
	int ret;
	const ssize_t mem_size = retained_mem_size(dev);

	if (!retained_mem_bbram_inited) {
		return -ENOTSUP;
	}

	/* Make sure there is additional space for CRC and check
	 * retained_mem_size error.
	 */
	if ((mem_size < CRC_SIZE) ||
	    ((offset + size) > (mem_size - CRC_SIZE))) {
		return -EINVAL;
	}
	ret = retained_mem_write(dev, offset, data, size);
	if (ret) {
		return ret;
	}

	return retained_mem_bbram_crc_update(dev);
}

int retained_mem_bbram_init(const struct device *dev)
{
	const ssize_t mem_size = retained_mem_size(dev);
	uint16_t crc, expected_crc;
	int ret;

	/* Handle too small memory and retained_mem_size error. */
	if (mem_size < CRC_SIZE) {
		return -EINVAL;
	}

	/* Read current CRC. */
	ret = retained_mem_read(dev, mem_size - CRC_SIZE, (uint8_t *)&crc,
				CRC_SIZE);
	if (ret) {
		return ret;
	}

	/* Calculate expected CRC. */
	ret = retained_mem_bbram_crc(dev, &expected_crc);
	if (ret) {
		return ret;
	}

	if (crc != expected_crc) {
		/* Clear memory and update CRC if case of mismatch. */
		ret = retained_mem_clear(dev);
		if (ret) {
			return ret;
		}
		ret = retained_mem_bbram_crc_update(dev);
		if (ret) {
			return ret;
		}
	}
	retained_mem_bbram_inited = true;

	return 0;
}
