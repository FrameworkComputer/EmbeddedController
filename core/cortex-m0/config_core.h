/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CONFIG_CORE_H
#define __CONFIG_CORE_H

/* Linker binary architecture and format */
#define BFD_ARCH arm
#define BFD_FORMAT "elf32-littlearm"

/* Emulate the CLZ instruction since the CPU core is lacking support */
#define CONFIG_SOFTWARE_CLZ
#define CONFIG_SOFTWARE_PANIC

#endif /* __CONFIG_CORE_H */
