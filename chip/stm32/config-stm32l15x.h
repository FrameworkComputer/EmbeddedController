/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_SIZE 0x00020000
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_FLASH_ERASE_SIZE 0x0100  /* erase bank size */

/*
 * TODO(crosbug.com/p/23805): Technically we can write in word-mode (4 bytes at
 * a time), but that's really slow, and older host interfaces which can't ask
 * about the ideal size would then end up writing in that mode instead of the
 * faster page mode.  So lie about the write size for now.  Once all software
 * (flashrom, u-boot, ectool) which cares has been updated to know about ver.1
 * of EC_CMD_GET_FLASH_INFO, we can remove this workaround.
 */
#define CONFIG_FLASH_WRITE_SIZE 0x0080

/* Ideal write size in page-mode */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x0080

#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00004000

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 45

/* Lots of RAM, so use bigger UART buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048

/* Use DMA for UART receive */
#define CONFIG_UART_RX_DMA

/* Flash erases to 0, not 1 */
#define CONFIG_FLASH_ERASED_VALUE32 0

/* USB packet ram config */
#define CONFIG_USB_RAM_BASE        0x40006000
#define CONFIG_USB_RAM_SIZE        512
#define CONFIG_USB_RAM_ACCESS_TYPE uint32_t
#define CONFIG_USB_RAM_ACCESS_SIZE 4
