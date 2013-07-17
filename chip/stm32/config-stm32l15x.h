/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_BASE       0x08000000
#define CONFIG_FLASH_PHYSICAL_SIZE 0x00020000
#define CONFIG_FLASH_SIZE       CONFIG_FLASH_PHYSICAL_SIZE
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_FLASH_ERASE_SIZE 0x0100  /* erase bank size */

/*
 * TODO(rspangler): Technically we can write in word-mode (4 bytes at a time),
 * but that's really slow, and older host interfaces which can't ask about the
 * ideal size would then end up writing in that mode instead of the faster page
 * mode.  So lie about the write size for now.  Once all software (flashrom,
 * u-boot, ectool) which cares has been updated to know about ver.1 of
 * EC_CMD_GET_FLASH_INFO, we can remove this workaround.
 */
#define CONFIG_FLASH_WRITE_SIZE 0x0080

/* Ideal write size in page-mode */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x0080

#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00004000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (64 * 1024)

#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_RO_SIZE       (CONFIG_FW_IMAGE_SIZE - CONFIG_FW_PSTATE_SIZE)
#define CONFIG_FW_RW_OFF        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE       CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_WP_RO_OFF     CONFIG_FW_RO_OFF
#define CONFIG_FW_WP_RO_SIZE    CONFIG_FW_RO_SIZE

/*
 * Put pstate after RO to give RW more space and make RO write protect
 * region contiguous.
 */
#define CONFIG_FW_PSTATE_SIZE  CONFIG_FLASH_BANK_SIZE
#define CONFIG_FW_PSTATE_OFF   (CONFIG_FW_RO_OFF + CONFIG_FW_RO_SIZE)

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 45

/* Lots of RAM, so use bigger UART buffer */
#define CONFIG_UART_TX_BUF_SIZE 2048

/* Flash erases to 0, not 1 */
#define CONFIG_FLASH_ERASED_VALUE32 0
