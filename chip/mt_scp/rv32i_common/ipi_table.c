/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * IPI handlers declaration
 */

#include "common.h"
#include "ipi_chip.h"

typedef void (*ipi_handler_t)(int32_t id, void *data, uint32_t len);

#ifndef PASS
#define PASS 1
#endif

#define ipi_arguments int32_t id, void *data, uint32_t len

#if PASS == 1
void ipi_handler_undefined(ipi_arguments) { }

const int ipi_wakeup_undefined;

#define table(type, name, x) x

#define ipi_x_func(suffix, args, number)                                       \
	extern void __attribute__(                                             \
		(used, weak, alias(STRINGIFY(ipi_##suffix##_undefined))))      \
		ipi_##number##_##suffix(args);

#define ipi_x_var(suffix, number)                                              \
	extern int __attribute__(                                              \
		(weak, alias(STRINGIFY(ipi_##suffix##_undefined))))            \
		ipi_##number##_##suffix;

#endif /* PASS == 1 */

#if PASS == 2

#undef table
#undef ipi_x_func
#undef ipi_x_var

#define table(type, name, x)                                                   \
	type const name[]                                                      \
		__attribute__((aligned(4), used, section(".rodata.ipi"))) = {x}

#define ipi_x_var(suffix, number)                                              \
	[number < IPI_COUNT ? number : -1] = &ipi_##number##_##suffix,

#define ipi_x_func(suffix, args, number) ipi_x_var(suffix, number)

#endif /* PASS == 2 */

/*
 * Include generated IPI table (by util/gen_ipi_table). The contents originate
 * from IPI_COUNT definition in board.h
 */
#include "ipi_table_gen.inc"

#if PASS == 1
#undef PASS
#define PASS 2
#include "ipi_table.c"
BUILD_ASSERT(ARRAY_SIZE(ipi_handler_table) == IPI_COUNT);
BUILD_ASSERT(ARRAY_SIZE(ipi_wakeup_table) == IPI_COUNT);
#endif
