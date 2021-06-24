/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT nuvoton_npcx_cros_bbram

#include <drivers/cros_bbram.h>
#include <drivers/cros_system.h>
#include <errno.h>
#include <logging/log.h>
#include <sys/util.h>

LOG_MODULE_REGISTER(cros_bbram, LOG_LEVEL_ERR);

/* Device config */
struct cros_bbram_npcx_config {
	/* BBRAM base address */
	uintptr_t base_addr;
	/* BBRAM size (Unit:bytes) */
	int size;
	/* Status register base address */
	uintptr_t status_reg_addr;
};

#define NPCX_STATUS_IBBR BIT(7)
#define NPCX_STATUS_VSBY BIT(1)
#define NPCX_STATUS_VCC1 BIT(0)

#define DRV_CONFIG(dev) ((const struct cros_bbram_npcx_config *)(dev)->config)
#define DRV_STATUS(dev) \
	(*((volatile uint8_t *)DRV_CONFIG(dev)->status_reg_addr))

static int cros_bbram_npcx_ibbr(const struct device *dev)
{
	return DRV_STATUS(dev) & NPCX_STATUS_IBBR;
}

static int cros_bbram_npcx_reset_ibbr(const struct device *dev)
{
	DRV_STATUS(dev) = NPCX_STATUS_IBBR;
	return 0;
}

static int cros_bbram_npcx_vsby(const struct device *dev)
{
	return DRV_STATUS(dev) & NPCX_STATUS_VSBY;
}

static int cros_bbram_npcx_reset_vsby(const struct device *dev)
{
	DRV_STATUS(dev) = NPCX_STATUS_VSBY;
	return 0;
}

static int cros_bbram_npcx_vcc1(const struct device *dev)
{
	return DRV_STATUS(dev) & NPCX_STATUS_VCC1;
}

static int cros_bbram_npcx_reset_vcc1(const struct device *dev)
{
	DRV_STATUS(dev) = NPCX_STATUS_VCC1;
	return 0;
}

static int cros_bbram_npcx_read(const struct device *dev, int offset, int size,
				uint8_t *data)
{
	const struct cros_bbram_npcx_config *config = DRV_CONFIG(dev);

	if (offset < 0 || size < 1 || offset + size >= config->size ||
	    cros_bbram_npcx_ibbr(dev)) {
		return -EFAULT;
	}

	for (size_t i = 0; i < size; ++i) {
		*(data + i) =
			*((volatile uint8_t *)config->base_addr + offset + i);
	}
	return 0;
}

static int cros_bbram_npcx_write(const struct device *dev, int offset, int size,
				 uint8_t *data)
{
	const struct cros_bbram_npcx_config *config = DRV_CONFIG(dev);

	if (offset < 0 || size < 1 || offset + size >= config->size ||
	    cros_bbram_npcx_ibbr(dev)) {
		return -EFAULT;
	}

	for (size_t i = 0; i < size; ++i) {
		*((volatile uint8_t *)config->base_addr + offset + i) =
			*(data + i);
	}
	return 0;
}

static const struct cros_bbram_driver_api cros_bbram_npcx_driver_api = {
	.ibbr = cros_bbram_npcx_ibbr,
	.reset_ibbr = cros_bbram_npcx_reset_ibbr,
	.vsby = cros_bbram_npcx_vsby,
	.reset_vsby = cros_bbram_npcx_reset_vsby,
	.vcc1 = cros_bbram_npcx_vcc1,
	.reset_vcc1 = cros_bbram_npcx_reset_vcc1,
	.read = cros_bbram_npcx_read,
	.write = cros_bbram_npcx_write,
};

static int bbram_npcx_init(const struct device *dev)
{
	const struct device *sys_dev = device_get_binding("CROS_SYSTEM");
	int reset = cros_system_get_reset_cause(sys_dev);

	if (reset == POWERUP) {
		/* clear the status register when EC power-up*/
		DRV_STATUS(dev) = NPCX_STATUS_IBBR | NPCX_STATUS_VSBY |
				  NPCX_STATUS_VCC1;
	}

	return 0;
}

/*
 * The priority of bbram_npcx_init() should lower than cros_system_npcx_init().
 */
#if (CONFIG_CROS_BBRAM_NPCX_INIT_PRIORITY <= \
     CONFIG_CROS_SYSTEM_NPCX_INIT_PRIORITY)
#error CONFIG_CROS_BBRAM_NPCX_INIT_PRIORITY must greater than \
	CONFIG_CROS_SYSTEM_NPCX_INIT_PRIORITY
#endif

#define CROS_BBRAM_INIT(inst)                                                \
	static struct {                                                      \
	} cros_bbram_data_##inst;                                            \
	static const struct cros_bbram_npcx_config cros_bbram_cfg_##inst = { \
		.base_addr = DT_INST_REG_ADDR_BY_NAME(inst, memory),         \
		.size = DT_INST_REG_SIZE_BY_NAME(inst, memory),              \
		.status_reg_addr = DT_INST_REG_ADDR_BY_NAME(inst, status),   \
	};                                                                   \
	DEVICE_DT_INST_DEFINE(inst,                                          \
			      bbram_npcx_init, NULL,                         \
			      &cros_bbram_data_##inst,                       \
			      &cros_bbram_cfg_##inst, PRE_KERNEL_1,          \
			      CONFIG_CROS_BBRAM_NPCX_INIT_PRIORITY,          \
			      &cros_bbram_npcx_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CROS_BBRAM_INIT);
