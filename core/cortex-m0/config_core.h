/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CORE_H
#define __CROS_EC_CONFIG_CORE_H

/* Linker binary architecture and format */
#define BFD_ARCH arm
#define BFD_FORMAT "elf32-littlearm"

/* Emulate the CLZ/CTZ instructions since the CPU core is lacking support */
#define CONFIG_SOFTWARE_CLZ
#define CONFIG_SOFTWARE_CTZ
#define CONFIG_SOFTWARE_PANIC

#define CONFIG_ASSEMBLY_MULA32

#endif /* __CROS_EC_CONFIG_CORE_H */
