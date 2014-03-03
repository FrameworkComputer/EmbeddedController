/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CONFIG_CORE_H
#define __CONFIG_CORE_H

/* Linker binary architecture and format */
#define BFD_ARCH nds32
#define BFD_FORMAT "elf32-nds32le"

/*
 * The Andestar v3m architecture has no CLZ instruction (contrary to v3),
 * so let's use the software implementation.
 */
#define CONFIG_SOFTWARE_CLZ

/*
 * Force the compiler to use a proper relocation when accessing an external
 * variable in a read-only section.
 * TODO(crosbug.com/p/24378): remove me when the nds32 toolchain bug is fixed.
 */
#undef RO
#define RO(var) \
({								\
	typeof(var) *__ptr_val;					\
	asm volatile("la %0, " #var "\n" : "=r"(__ptr_val));	\
	((typeof(var))(*__ptr_val));				\
})

#endif /* __CONFIG_CORE_H */
