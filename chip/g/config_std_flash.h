/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_STD_FLASH_H
#define __CROS_EC_CONFIG_STD_FLASH_H

/* RO firmware must start at beginning of flash */
#define CONFIG_FW_RO_OFF		0

/*
 * The EC uses the one bank of flash to emulate a SPI-like write protect
 * register with persistent state.
 */
#define CONFIG_FW_PSTATE_SIZE		CONFIG_FLASH_BANK_SIZE

/* PSTATE immediately follows RO, in the first half of flash */
#define CONFIG_FW_RO_SIZE		(CONFIG_FW_IMAGE_SIZE		\
					 - CONFIG_FW_PSTATE_SIZE)
#define CONFIG_FW_PSTATE_OFF		CONFIG_FW_RO_SIZE
#define CONFIG_FLASH_SIZE		CONFIG_FLASH_PHYSICAL_SIZE

/* RW firmware is one firmware image offset from the start */
#define CONFIG_FW_RW_OFF		CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE		CONFIG_FW_IMAGE_SIZE

/* TODO(crosbug.com/p/23796): why 2 sets of configs with the same numbers? */
#define CONFIG_FW_WP_RO_OFF		CONFIG_FW_RO_OFF
#define CONFIG_FW_WP_RO_SIZE		CONFIG_FW_RO_SIZE

#endif	/* __CROS_EC_CONFIG_STD_FLASH_H */
