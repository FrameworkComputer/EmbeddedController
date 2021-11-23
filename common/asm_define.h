/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_COMMON_ASM_DEFINE_H
#define __CROS_EC_COMMON_ASM_DEFINE_H

#include "stddef.h"

/*
 * Use an immediate integer constraint
 * (https://gcc.gnu.org/onlinedocs/gcc/Simple-Constraints.html) to write the
 * value. This file is compiled with the "-S" flag, which stops the compiler
 * after generating assembly. The resulting assembly is then grepped for the
 * "__ASM_DEFINE__" strings, which is used to create a header file with the
 * value.
 */
#define ASM_DEFINE(NAME, VAL) \
	__asm__ volatile(".ascii \" __ASM_DEFINE__ " NAME " %0\"" : : "i"(VAL))

#define ASM_DEFINE_OFFSET(NAME, TYPE, MEMBER) \
	ASM_DEFINE(NAME, offsetof(TYPE, MEMBER))

#endif /* __CROS_EC_COMMON_ASM_DEFINE_H */
