/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2026 The ChromiumOS Authors */

#ifndef __CONFIG_CHIP_H
#define __CONFIG_CHIP_H

#define CONFIG_FLASH_WRITE_IDEAL_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), write_block_size)
#define CONFIG_FLASH_ERASE_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), erase_block_size)

#endif /* __CONFIG_CHIP_H */
