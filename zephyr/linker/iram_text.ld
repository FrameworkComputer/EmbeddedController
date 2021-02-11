/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CONFIG_ARCH_POSIX

/* This code taken from core/cortex-m/ec.lds.S */

#if defined(CONFIG_PLATFORM_EC_MPU)
/* MPU regions must be aligned to a 32-byte boundary */
#define _IRAM_ALIGN	32
#else
#define _IRAM_ALIGN	4
#endif

	. = ALIGN(_IRAM_ALIGN);
	__iram_text_start = .;
	*(.iram.text)
	. = ALIGN(_IRAM_ALIGN);
	__iram_text_end = .;

#endif /* CONFIG_ARCH_POSIX */
