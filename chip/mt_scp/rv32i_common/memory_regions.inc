/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
REGION(dramnc_bss, rw, DRAM_NC_BASE, DRAM_NC_SIZE) /* DRAM uncached 1M-4K */
REGION(panic, !rwx, CONFIG_PANIC_DRAM_BASE, CONFIG_PANIC_DRAM_SIZE) /* panic data */
REGION(kernel, !rwx, KERNEL_BASE, KERNEL_SIZE) /* kernel allocable  */
