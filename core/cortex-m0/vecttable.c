/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cortex-M CPU vector table
 */

#ifndef ___INIT
#define ___INIT
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "panic-internal.h"
#include "task.h"
#endif  /* __INIT */

typedef void (*func)(void);

#ifndef PASS
#define PASS 1
#endif

#if PASS == 1

void __attribute__((naked)) default_handler(void)
{
	/*
	 * An (enforced) long tail call to preserve exn_return in lr without
	 * restricting the relative placement of default_handler and
	 * exception_panic.
	 */
	asm volatile("bx %0\n" : : "r" (exception_panic));
}

#define table(x) x

/* Note: the alias target must be defined in this translation unit */
#define weak_with_default __attribute__((used, weak, alias("default_handler")))

#define vec(name) extern void weak_with_default name ## _handler(void);
#define irq(num) vec(irq_ ## num)

#define item(name) extern void name(void);
#define null

extern void stack_end(void); /* not technically correct, it's just a pointer */
extern void reset(void);

#pragma GCC diagnostic push
#if __GNUC__ >= 8
#pragma GCC diagnostic ignored "-Wattribute-alias"
#endif
#pragma GCC diagnostic pop

#endif /* PASS 1 */

#if PASS == 2
#undef table
#undef vec
#undef irq
#undef item
#undef null

/* number of elements before the first irq vector */
#define IRQ_OFFSET 16
/* element in the table that is null: extra IRQs are routed there,
 * then finally overwritten
 */
#define IRQ_UNUSED_OFFSET 8

#define table(x) func vectors[] __attribute__((section(".text.vecttable,\"a\" @"))) = { x[IRQ_UNUSED_OFFSET] = null };

#define vec(name) name ## _handler,
#define irq(num) [num < CONFIG_IRQ_COUNT ? num + IRQ_OFFSET : IRQ_UNUSED_OFFSET] = vec(irq_ ## num)

#define item(name) name,
#define null (void *)0,
#endif /* PASS 2 */

table(
	item(stack_end)
	item(reset)
	vec(nmi)
	vec(hard_fault)
	vec(mpu_fault)
	vec(bus_fault)
	vec(usage_fault)
	null
	null
	null
	null
	vec(svc)
	vec(debug)
	null
	vec(pendsv)
	vec(sys_tick)
	irq(0)
	irq(1)
	irq(2)
	irq(3)
	irq(4)
	irq(5)
	irq(6)
	irq(7)
	irq(8)
	irq(9)
	irq(10)
	irq(11)
	irq(12)
	irq(13)
	irq(14)
	irq(15)
	irq(16)
	irq(17)
	irq(18)
	irq(19)
	irq(20)
	irq(21)
	irq(22)
	irq(23)
	irq(24)
	irq(25)
	irq(26)
	irq(27)
	irq(28)
	irq(29)
	irq(30)
	irq(31)
)

#if PASS == 1
#undef PASS
#define PASS 2
#include "vecttable.c"
#endif
