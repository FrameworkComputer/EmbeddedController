/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT ite_it8xxx2_cros_bbram

#include <drivers/cros_bbram.h>
#include <errno.h>
#include <logging/log.h>
#include <sys/util.h>

LOG_MODULE_REGISTER(cros_bbram, LOG_LEVEL_ERR);

/* Device config */
struct cros_bbram_it8xxx2_config {
	/* BBRAM base address */
	uintptr_t base_addr;
	/* BBRAM size (Unit:bytes) */
	int size;
};

#define DRV_CONFIG(dev) \
	((const struct cros_bbram_it8xxx2_config *)(dev)->config)

static int cros_bbram_it8xxx2_read(const struct device *dev, int offset,
				   int size, uint8_t *data)
{
	const struct cros_bbram_it8xxx2_config *config = DRV_CONFIG(dev);

	if (offset < 0 || size < 1 || offset + size >= config->size) {
		return -EFAULT;
	}

	for (size_t i = 0; i < size; ++i) {
		*(data + i) =
			*((volatile uint8_t *)config->base_addr + offset + i);
	}
	return 0;
}

static int cros_bbram_it8xxx2_write(const struct device *dev, int offset,
				    int size, uint8_t *data)
{
	const struct cros_bbram_it8xxx2_config *config = DRV_CONFIG(dev);

	if (offset < 0 || size < 1 || offset + size >= config->size) {
		return -EFAULT;
	}

	for (size_t i = 0; i < size; ++i) {
		*((volatile uint8_t *)config->base_addr + offset + i) =
			*(data + i);
	}
	return 0;
}

static const struct cros_bbram_driver_api cros_bbram_it8xxx2_driver_api = {
	.read = cros_bbram_it8xxx2_read,
	.write = cros_bbram_it8xxx2_write,
};

static int bbram_it8xxx2_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static const struct cros_bbram_it8xxx2_config cros_bbram_cfg = {
	.base_addr = DT_INST_REG_ADDR_BY_NAME(0, memory),
	.size = DT_INST_REG_SIZE_BY_NAME(0, memory),
};

DEVICE_DT_INST_DEFINE(0, bbram_it8xxx2_init, NULL, NULL, &cros_bbram_cfg,
		      PRE_KERNEL_1, CONFIG_CROS_BBRAM_IT8XXX2_INIT_PRIORITY,
		      &cros_bbram_it8xxx2_driver_api);
