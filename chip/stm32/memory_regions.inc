/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifdef CONFIG_USB_RAM_SIZE
REGION(usb_ram, rw, CONFIG_USB_RAM_BASE, \
       CONFIG_USB_RAM_SIZE * CONFIG_USB_RAM_ACCESS_SIZE / 2)
#endif /* CONFIG_USB_RAM_SIZE */
#ifdef CHIP_VARIANT_STM32H7X3
REGION(itcm,   wx, 0x00000000, 0x10000) /* CPU ITCM: 64kB */
REGION(dtcm,   rw, 0x20000000, 0x20000) /* CPU DTCM: 128kB */
REGION(ahb,    rw, 0x30000000, 0x48000) /* AHB-SRAM1-3: 288 kB */
REGION(ahb4,   rw, 0x38000000, 0x10000) /* AHB-SRAM4: 64kB */
REGION(backup, rw, 0x38800000, 0x01000) /* Backup RAM: 4kB */
#endif /* CHIP_VARIANT_STM32H7X3 */
