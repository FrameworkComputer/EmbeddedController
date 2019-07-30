/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

#if defined(BOARD)
#include "core/cortex-m/config_core.h"
#include "hw_regdefs.h"
#endif

/* Describe the RAM layout */
#define CONFIG_RAM_BASE         0x10000
#define CONFIG_RAM_SIZE         0x10000

/* Flash chip specifics */
#define CONFIG_FLASH_BANK_SIZE         0x800	/* protect bank size */
#define CONFIG_FLASH_ERASE_SIZE        0x800	/* erase bank size */
/* This flash can only be written as 4-byte words (aligned properly, too). */
#define CONFIG_FLASH_WRITE_SIZE        4	/* min write size (bytes) */
/* But we have a 32-word buffer for writing multiple adjacent cells */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE  128	/* best write size (bytes) */
/* The flash controller prevents bulk writes that cross row boundaries */
#define CONFIG_FLASH_ROW_SIZE          256	/* row size */

/* Describe the flash layout */
#define CONFIG_PROGRAM_MEMORY_BASE     0x40000
#define CONFIG_FLASH_SIZE              (512 * 1024)
#define CONFIG_FLASH_ERASED_VALUE32    (-1U)

#undef CONFIG_RO_HEAD_ROOM
#define CONFIG_RO_HEAD_ROOM	       1024	/* Room for ROM signature. */
#undef CONFIG_RW_HEAD_ROOM
#define CONFIG_RW_HEAD_ROOM	       CONFIG_RO_HEAD_ROOM  /* same for RW */

/* Memory-mapped internal flash */
#define CONFIG_INTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE

/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE CONFIG_PROGRAM_MEMORY_BASE

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* System stack size */
#define CONFIG_STACK_SIZE 1024

/* Idle task stack size */
#define IDLE_TASK_STACK_SIZE 512

/* Default task stack size */
#define TASK_STACK_SIZE 488

/* Larger task stack size, for hook task */
#define LARGER_TASK_STACK_SIZE 640

/* Magic for gpio.inc */
#define GPIO_PIN(port, index) (port), (1 << (index))
#define GPIO_PIN_MASK(p, m) .port = (p), .mask = (m)
#define DUMMY_GPIO_BANK 0

#define PCLK_FREQ  (24 * 1000 * 1000)

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT (GC_INTERRUPTS_COUNT - 15)

/* We'll have some special commands of our own */
#define CONFIG_EXTENSION_COMMAND 0xbaccd00a

/* Chip needs to do custom pre-init */
#define CONFIG_CHIP_PRE_INIT

/*
 * The flash memory is implemented in two halves. The SoC bootrom will look for
 * the first-stage bootloader at the beginning of each of the two halves and
 * prefer the newer one if both are valid. In EC terminology the bootloader
 * would be called the RO firmware, so we actually have two, not one. The
 * bootloader also looks in each half of the flash for a valid RW firmware, so
 * we have two possible RW images as well. The RO and RW images are not tightly
 * coupled, so either RO image can choose to boot either RW image.
 *
 * The EC firmware configuration is not (yet?) prepared to handle multiple,
 * non-contiguous, RO/RW combinations, so there's a bit of hackery to make this
 * work.
 *
 * The following macros try to make this all work.
 */

/* This isn't optional, since the bootrom will always look for both */
#define CHIP_HAS_RO_B

/* It's easier for us to consider each half as having its own RO and RW */
#define CFG_FLASH_HALF (CONFIG_FLASH_SIZE >> 1)

/*
 * We'll reserve some space at the top of each flash half for persistent
 * storage and other stuff that's not part of the RW image. We don't promise to
 * use these two areas for the same thing, it's just more convenient to make
 * them the same size.
 */
#define CFG_TOP_SIZE  0x3800
#define CFG_TOP_A_OFF (CFG_FLASH_HALF - CFG_TOP_SIZE)
#define CFG_TOP_B_OFF (CONFIG_FLASH_SIZE - CFG_TOP_SIZE)

/* The RO images start at the very beginning of each flash half */
#define CONFIG_RO_MEM_OFF 0
#define CHIP_RO_B_MEM_OFF CFG_FLASH_HALF

/* Size reserved for each RO image */
#define CONFIG_RO_SIZE 0x4000

/*
 * RW images start right after the reserved-for-RO areas in each half, but only
 * because that's where the RO images look for them. It's not a HW constraint.
 */
#define CONFIG_RW_MEM_OFF CONFIG_RO_SIZE
#define CONFIG_RW_B_MEM_OFF (CFG_FLASH_HALF + CONFIG_RW_MEM_OFF)

/* Size reserved for each RW image */
#define CONFIG_RW_SIZE (CFG_FLASH_HALF - CONFIG_RW_MEM_OFF - CFG_TOP_SIZE)

/*
 * These are needed in a couple of places, but aren't very meaningful. Because
 * we have two RO and two RW images, these values don't really match what's
 * described in the EC Image Geometry Spec at www.chromium.org.
 */
/* TODO(wfrichar): Make them meaningful or learn to do without */
#define CONFIG_EC_PROTECTED_STORAGE_OFF    0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE   CONFIG_FLASH_SIZE
#define CONFIG_EC_WRITABLE_STORAGE_OFF     0
#define CONFIG_EC_WRITABLE_STORAGE_SIZE	   CONFIG_FLASH_SIZE
#define CONFIG_RO_STORAGE_OFF              0
#define CONFIG_RW_STORAGE_OFF              0
#define CONFIG_WP_STORAGE_OFF		   0
#define CONFIG_WP_STORAGE_SIZE		   CONFIG_EC_PROTECTED_STORAGE_SIZE

/*
 * Note: early versions of the SoC would let us build and manually sign our own
 * bootloaders, and the RW images could be self-signed. Production SoCs require
 * officially-signed binary blobs to use for the RO bootloader(s), and the RW
 * images that we build must be manually signed. So even though we generate RO
 * firmware images, they may not be useful.
 */
#define CONFIG_CUSTOMIZED_RO

/* Number of I2C ports */
#define I2C_PORT_COUNT 2

#define CONFIG_FLASH_LOG_SPACE CONFIG_FLASH_BANK_SIZE

/*
 * Flash log occupies space in the top of RO_B section, its counterpart in
 * RO_A is occupied by the certs.
 */
#define CONFIG_FLASH_LOG_BASE                                                  \
	(CONFIG_PROGRAM_MEMORY_BASE + CHIP_RO_B_MEM_OFF + CONFIG_RO_SIZE -     \
	 CONFIG_FLASH_LOG_SPACE)
#endif /* __CROS_EC_CONFIG_CHIP_H */
