/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CORE_H
#define __CROS_EC_CONFIG_CORE_H

/* Linker binary architecture and format */
#define BFD_ARCH "i386"
#define BFD_FORMAT "elf32-i386"

#define CONFIG_SOFTWARE_PANIC

/*
 * Since all implementations minute-ia are a single core, we do not need a
 * "lock;" prefix on any instructions. We use the below define in places where
 * a lock statement would be needed if there were multiple cores.
 *
 * Also the destination operand needs to be a memory location instead of a
 * register for us to drop the "lock;" prefix for a single-core chip.
 */
#ifndef __ASSEMBLER__
#define ASM_LOCK_PREFIX ""
#else
#define ASM_LOCK_PREFIX
#endif

/*
 * Flag indicates the task uses FPU H/W
 */
#define MIA_TASK_FLAG_USE_FPU                   0x00000001

#endif /* __CROS_EC_CONFIG_CORE_H */
