/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * MEC1322 SoC little FW
 *
 */

#include <stdnoreturn.h>

noreturn void lfw_main(void) __attribute__((naked));
void fault_handler(void) __attribute__((naked));

struct int_vector_t {
	void *stack_ptr;
	void *reset_vector;
	void *nmi;
	void *hard_fault;
	void *bus_fault;
	void *usage_fault;
};

#define SPI_CHUNK_SIZE 1024
