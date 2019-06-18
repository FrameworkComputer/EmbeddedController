/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cortex-M CPU vector table
 */

#ifndef ___INIT
#define ___INIT
#include "config.h"
#include <task.h>
#endif

typedef void (*func)(void);

#ifndef PASS
#define PASS 1
#endif

#if PASS == 1
/* Default exception handler */
void __attribute__((used, naked)) default_handler(void);
void default_handler()
{
	asm(
	".thumb_func\n"
	"	b exception_panic"
	);
}

#define table(x) x

#define weak_with_default __attribute__((used,weak,alias("default_handler")))

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
/* Call default_handler if svc_handler is not found (task.c is not built) */
void weak_with_default svc_handler(int desched, task_id_t resched);
#pragma GCC diagnostic pop

/*
 * SVC handler helper
 *
 * Work around issue where a late exception can corrupt r0 to r3,
 * see section 2.7 (svc) of Cortex-M3 Application Note 179:
 * http://infocenter.arm.com/help/topic/com.arm.doc.dai0179b/AppsNote179.pdf
 *
 * This approach differs slightly from the one in the document,
 * it only loads r0 (desched) and r1 (resched) for svc_handler.
 */
void __attribute__((used,naked)) svc_helper_handler(void);
void svc_helper_handler()
{
	asm(
	".thumb_func\n"
	"	tst lr, #4		/* see if called from supervisor mode */\n"
	"	mrs r2, msp		/* get the correct stack pointer into r2 */\n"
	"	it ne\n"
	"	mrsne r2, psp\n"
	"	ldr r1, [r2, #4]	/* get regs from stack frame */\n"
	"	ldr r0, [r2]\n"
	"	b %0			/* call svc_handler */\n"
	:
	: "i"(svc_handler)
	);
}

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

#define table(x)								\
	const func vectors[] __attribute__((section(".text.vecttable"))) = {	\
		x								\
		[IRQ_UNUSED_OFFSET] = null					\
	};

#define vec(name) name ## _handler,
#define irq(num) [num < CONFIG_IRQ_COUNT ? num + IRQ_OFFSET : IRQ_UNUSED_OFFSET] = vec(irq_ ## num)

#define item(name) name,
#define null (void*)0,
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
	item(svc_helper_handler)
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
	irq(32)
	irq(33)
	irq(34)
	irq(35)
	irq(36)
	irq(37)
	irq(38)
	irq(39)
	irq(40)
	irq(41)
	irq(42)
	irq(43)
	irq(44)
	irq(45)
	irq(46)
	irq(47)
	irq(48)
	irq(49)
	irq(50)
	irq(51)
	irq(52)
	irq(53)
	irq(54)
	irq(55)
	irq(56)
	irq(57)
	irq(58)
	irq(59)
	irq(60)
	irq(61)
	irq(62)
	irq(63)
	irq(64)
	irq(65)
	irq(66)
	irq(67)
	irq(68)
	irq(69)
	irq(70)
	irq(71)
	irq(72)
	irq(73)
	irq(74)
	irq(75)
	irq(76)
	irq(77)
	irq(78)
	irq(79)
	irq(80)
	irq(81)
	irq(82)
	irq(83)
	irq(84)
	irq(85)
	irq(86)
	irq(87)
	irq(88)
	irq(89)
	irq(90)
	irq(91)
	irq(92)
	irq(93)
	irq(94)
	irq(95)
	irq(96)
	irq(97)
	irq(98)
	irq(99)
	irq(100)
	irq(101)
	irq(102)
	irq(103)
	irq(104)
	irq(105)
	irq(106)
	irq(107)
	irq(108)
	irq(109)
	irq(110)
	irq(111)
	irq(112)
	irq(113)
	irq(114)
	irq(115)
	irq(116)
	irq(117)
	irq(118)
	irq(119)
	irq(120)
	irq(121)
	irq(122)
	irq(123)
	irq(124)
	irq(125)
	irq(126)
	irq(127)
	irq(128)
	irq(129)
	irq(130)
	irq(131)
	irq(132)
	irq(133)
	irq(134)
	irq(135)
	irq(136)
	irq(137)
	irq(138)
	irq(139)
	irq(140)
	irq(141)
	irq(142)
	irq(143)
	irq(144)
	irq(145)
	irq(146)
	irq(147)
	irq(148)
	irq(149)
	irq(150)
	irq(151)
	irq(152)
	irq(153)
	irq(154)
	irq(155)
	irq(156)
	irq(157)
	irq(158)
	irq(159)
	irq(160)
	irq(161)
	irq(162)
	irq(163)
	irq(164)
	irq(165)
	irq(166)
	irq(167)
	irq(168)
	irq(169)
	irq(170)
	irq(171)
	irq(172)
	irq(173)
	irq(174)
	irq(175)
	irq(176)
	irq(177)
	irq(178)
	irq(179)
	irq(180)
	irq(181)
	irq(182)
	irq(183)
	irq(184)
	irq(185)
	irq(186)
	irq(187)
	irq(188)
	irq(189)
	irq(190)
	irq(191)
	irq(192)
	irq(193)
	irq(194)
	irq(195)
	irq(196)
	irq(197)
	irq(198)
	irq(199)
	irq(200)
	irq(201)
	irq(202)
	irq(203)
	irq(204)
	irq(205)
	irq(206)
	irq(207)
	irq(208)
	irq(209)
	irq(210)
	irq(211)
	irq(212)
	irq(213)
	irq(214)
	irq(215)
	irq(216)
	irq(217)
	irq(218)
	irq(219)
	irq(220)
	irq(221)
	irq(222)
	irq(223)
	irq(224)
	irq(225)
	irq(226)
	irq(227)
	irq(228)
	irq(229)
	irq(230)
	irq(231)
	irq(232)
	irq(233)
	irq(234)
	irq(235)
	irq(236)
	irq(237)
	irq(238)
	irq(239)
	irq(240)
	irq(241)
	irq(242)
	irq(243)
	irq(244)
	irq(245)
	irq(246)
	irq(247)
	irq(248)
	irq(249)
	irq(250)
	irq(251)
	irq(252)
	irq(253)
	irq(254)
)

#if PASS == 1
#undef PASS
#define PASS 2
#include "vecttable.c"
#endif
