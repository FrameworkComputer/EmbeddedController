/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MCHP MEC SoC little FW
 *
 */

#include "common.h"

#include <stdint.h>

/* Why naked?  This is dangerous except for
 * function/ISR wrappers using inline assembly.
 * lfw_main() makes many calls and has one local variable.
 * Naked C functions should not use local data unless the local
 * data can fit in CPU registers.
 * Note other C functions called by lfw_main() are not marked naked and
 * do include compiler generated prolog and epilog code.
 * We also do not know how much stack space is available when
 * EC_RO calls lfw_main().
 *
__noreturn void lfw_main(void) __attribute__ ((naked));
*/
__noreturn void lfw_main(void);
void fault_handler(void) __attribute__((naked));

/*
 * Defined in linker file ec_lfw.ld
 */
extern uint32_t lfw_stack_top[];

struct int_vector_t {
	void *stack_ptr;
	void *reset_vector;
	void *nmi;
	void *hard_fault;
	void *bus_fault;
	void *usage_fault;
};

#define SPI_CHUNK_SIZE 1024
