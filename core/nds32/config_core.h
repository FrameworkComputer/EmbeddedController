/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CORE_H
#define __CROS_EC_CONFIG_CORE_H

/* Linker binary architecture and format */
#define BFD_ARCH nds32
#define BFD_FORMAT "elf32-nds32le"

#define CONFIG_SOFTWARE_PANIC

/*
 * The Andestar v3m architecture has no CLZ/CTZ instructions (contrary to v3),
 * so let's use the software implementation.
 */
#define CONFIG_SOFTWARE_CLZ
#define CONFIG_SOFTWARE_CTZ

#endif /* __CROS_EC_CONFIG_CORE_H */
