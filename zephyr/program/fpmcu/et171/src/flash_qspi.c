/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "spi_flash_reg.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/andes_flash_xip_api_ex.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define flash_ctrl_dev DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller))
#define QSPI_INIT_PRIORITY 51

static int enable_qspi(void)
{
	int ret;
	struct andes_xip_ex_ops_mem_read_cmd_in rd_cmd = {
		.cmd = FLASH_ANDES_XIP_MEM_RD_CMD_EB,
	};
	struct andes_xip_ex_ops_set_in set_regs = {
		.regs = { 0 },
		.masks = { 0 },
	};

	set_regs.regs[1] = SPI_FLASH_SR2_QE;
	set_regs.masks[1] = SPI_FLASH_SR2_QE;
	ret = flash_ex_op(flash_ctrl_dev, FLASH_ANDES_XIP_EX_OP_SET_STATUS_REGS,
			  (uintptr_t)&set_regs, 0);
	if (ret) {
		return ret;
	}

	ret = flash_ex_op(flash_ctrl_dev, FLASH_ANDES_XIP_EX_OP_MEM_READ_CMD,
			  (uintptr_t)&rd_cmd, 0);

	return ret;
}
SYS_INIT(enable_qspi, POST_KERNEL, QSPI_INIT_PRIORITY);
BUILD_ASSERT(CONFIG_FLASH_ANDES_QSPI_INIT_PRIORITY < QSPI_INIT_PRIORITY);
