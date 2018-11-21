/* Copyright 2018 The Chromium OS Authors. All rights reserved.
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
	type name[] __aligned(4)                                               \
		__attribute__((section(".rodata.ipi, \"a\" @"))) = {x}

#define ipi_x_var(suffix, number)                                              \
	[number < IPI_COUNT ? number : -1] = &ipi_##number##_##suffix,

#define ipi_x_func(suffix, args, number) ipi_x_var(suffix, number)

#endif /* PASS == 2 */

/*
 * Table to hold all the IPI handler function pointer.
 */
table(ipi_handler_t, ipi_handler_table,
	ipi_x_func(handler, ipi_arguments, 0)
	ipi_x_func(handler, ipi_arguments, 1)
	ipi_x_func(handler, ipi_arguments, 2)
	ipi_x_func(handler, ipi_arguments, 3)
	ipi_x_func(handler, ipi_arguments, 4)
	ipi_x_func(handler, ipi_arguments, 5)
	ipi_x_func(handler, ipi_arguments, 6)
	ipi_x_func(handler, ipi_arguments, 7)
);

/*
 * Table to hold all the wake-up bool address.
 */
table(int*, ipi_wakeup_table,
	ipi_x_var(wakeup, 0)
	ipi_x_var(wakeup, 1)
	ipi_x_var(wakeup, 2)
	ipi_x_var(wakeup, 3)
	ipi_x_var(wakeup, 4)
	ipi_x_var(wakeup, 5)
	ipi_x_var(wakeup, 6)
	ipi_x_var(wakeup, 7)
);

#if PASS == 1
#undef PASS
#define PASS 2
#include "ipi_table.c"
#endif
