/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_BASE       0x08000000
#define CONFIG_FLASH_SIZE       0x00010000
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00002000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (32 * 1024)

#define CONFIG_NO_RW_B

#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_RO_SIZE       CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_A_OFF         CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_A_SIZE        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_B_OFF         (2 * CONFIG_FW_IMAGE_SIZE)
#define CONFIG_FW_B_SIZE        CONFIG_FW_IMAGE_SIZE

#define CONFIG_SECTION_RO_OFF   CONFIG_FW_RO_OFF
#define CONFIG_SECTION_RO_SIZE  CONFIG_FW_RO_SIZE
#define CONFIG_SECTION_A_OFF    CONFIG_FW_A_OFF
#define CONFIG_SECTION_A_SIZE   CONFIG_FW_A_SIZE
#define CONFIG_SECTION_B_OFF    CONFIG_FW_B_OFF
#ifdef CONFIG_NO_RW_B
#define CONFIG_SECTION_B_SIZE   0
#else /* CONFIG_NO_RW_B */
#define CONFIG_SECTION_B_SIZE   CONFIG_FW_B_SIZE
#endif /* CONFIG_NO_RW_B */

/* no keys for now */
#define CONFIG_VBOOT_ROOTKEY_OFF    (CONFIG_FW_RO_OFF + CONFIG_FW_RO_SIZE)
#define CONFIG_VBLOCK_A_OFF         (CONFIG_FW_A_OFF + CONFIG_FW_A_SIZE)
#define CONFIG_VBLOCK_B_OFF         (CONFIG_FW_B_OFF + CONFIG_FW_B_SIZE)
#define CONFIG_VBOOT_ROOTKEY_SIZE   0
#define CONFIG_VBLOCK_SIZE          0

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 61

/* Debug UART parameters for panic message */
#define CONFIG_UART_ADDRESS    0x40013800 /* USART1 */
#define CONFIG_UART_DR_OFFSET  0x04
#define CONFIG_UART_SR_OFFSET  0x00
#define CONFIG_UART_SR_TXEMPTY 0x80
