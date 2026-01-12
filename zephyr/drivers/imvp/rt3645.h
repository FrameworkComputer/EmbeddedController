/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_IMVP_RT3645_H_
#define ZEPHYR_DRIVERS_IMVP_RT3645_H_

#include <zephyr/device.h>
#include <zephyr/types.h>

#define RT3645_PAGE_GLOBAL 0x0

#define NVM_STAT_REG 0xEC
#define NVM_RELOAD_STAT_BIT 0x7
#define NVM_PRGRM_FINISH_STAT_BIT 0x6
#define NVM_STAT_BITS 0x0

#define NVM_PRGRM_CTRL_REG 0xED
#define NVM_PRGRM_DAT 0xAA
#define NVM_RESTORE_DAT 0x66

#define CONFIG_MODE_REG 0xF1
#define LOCK_CODE1 0x00
#define LOCK_CODE2 0xFF

#define PRODUCT_ID_REG 0xFE
#define PRODUCT_ID 0x45

#define PAGE_SET_REG 0xEF

#define RT3645_PAGE_D 0x0D
#define CRC_REG 0x13

struct rt3645_info {
	uint8_t page;
	uint8_t reg;
	uint8_t val;
};

int rt3645_read_reg(const struct device *dev, uint8_t reg, uint8_t *val);

int rt3645_write_reg(const struct device *dev, uint8_t reg, uint8_t val);

int rt3645_set_cfg_mode(const struct device *dev, const uint8_t *seq,
			uint8_t seq_len);

int rt3645_set_page(const struct device *dev, uint8_t page);

int rt3645_load_config(const struct device *dev);

int rt3645_store_config(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_IMVP_RT3645_H_ */
