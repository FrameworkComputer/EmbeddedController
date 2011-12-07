/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cortex-M CPU initialization
 */

#include "config.h"

.text

.syntax unified
.code 16

.macro vector name
.long \name\()_handler
.weak \name\()_handler
.set \name\()_handler, default_handler
.endm

/* Exceptions vector */
vectors:
.long stack_end       @ initial stack pointer
.long reset           @ reset handler
vector nmi            @ NMI handler
vector hard_fault     @ HardFault handler
vector mpu_fault      @ MPU fault handler
vector bus_fault      @ Bus fault handler
vector usage_fault    @ Usage fault handler
.long 0               @ reserved
.long 0               @ reserved
.long 0               @ reserved
.long 0               @ reserved
vector svc            @ SWI
vector debug          @ Debug handler
.long 0               @ reserved
vector pendsv         @ PendSV handler
vector sys_tick       @ SysTick handler
vector irq_0          @ IRQ 0 handler
vector irq_1          @ IRQ 1 handler
vector irq_2          @ IRQ 2 handler
vector irq_3          @ IRQ 3 handler
vector irq_4          @ IRQ 4 handler
vector irq_5          @ IRQ 5 handler
vector irq_6          @ IRQ 6 handler
vector irq_7          @ IRQ 7 handler
vector irq_8          @ IRQ 8 handler
vector irq_9          @ IRQ 9 handler
vector irq_10         @ IRQ 10 handler
vector irq_11         @ IRQ 11 handler
vector irq_12         @ IRQ 12 handler
vector irq_13         @ IRQ 13 handler
vector irq_14         @ IRQ 14 handler
vector irq_15         @ IRQ 15 handler
vector irq_16         @ IRQ 16 handler
vector irq_17         @ IRQ 17 handler
vector irq_18         @ IRQ 18 handler
vector irq_19         @ IRQ 19 handler
vector irq_20         @ IRQ 20 handler
vector irq_21         @ IRQ 21 handler
vector irq_22         @ IRQ 22 handler
vector irq_23         @ IRQ 23 handler
vector irq_24         @ IRQ 24 handler
vector irq_25         @ IRQ 25 handler
vector irq_26         @ IRQ 26 handler
vector irq_27         @ IRQ 27 handler
vector irq_28         @ IRQ 28 handler
vector irq_29         @ IRQ 29 handler
vector irq_30         @ IRQ 30 handler
vector irq_31         @ IRQ 31 handler
vector irq_32         @ IRQ 32 handler
vector irq_33         @ IRQ 33 handler
vector irq_34         @ IRQ 34 handler
vector irq_35         @ IRQ 35 handler
vector irq_36         @ IRQ 36 handler
vector irq_37         @ IRQ 37 handler
vector irq_38         @ IRQ 38 handler
vector irq_39         @ IRQ 39 handler
vector irq_40         @ IRQ 40 handler
vector irq_41         @ IRQ 41 handler
vector irq_42         @ IRQ 42 handler
vector irq_43         @ IRQ 43 handler
vector irq_44         @ IRQ 44 handler
vector irq_45         @ IRQ 45 handler
vector irq_46         @ IRQ 46 handler
vector irq_47         @ IRQ 47 handler
vector irq_48         @ IRQ 48 handler
vector irq_49         @ IRQ 49 handler
vector irq_50         @ IRQ 50 handler
vector irq_51         @ IRQ 51 handler
vector irq_52         @ IRQ 52 handler
vector irq_53         @ IRQ 53 handler
vector irq_54         @ IRQ 54 handler
vector irq_55         @ IRQ 55 handler
vector irq_56         @ IRQ 56 handler
vector irq_57         @ IRQ 57 handler
vector irq_58         @ IRQ 58 handler
vector irq_59         @ IRQ 59 handler
vector irq_60         @ IRQ 60 handler
vector irq_61         @ IRQ 61 handler
vector irq_62         @ IRQ 62 handler
vector irq_63         @ IRQ 63 handler
vector irq_64         @ IRQ 64 handler
vector irq_65         @ IRQ 65 handler
vector irq_66         @ IRQ 66 handler
vector irq_67         @ IRQ 67 handler
vector irq_68         @ IRQ 68 handler
vector irq_69         @ IRQ 69 handler
vector irq_70         @ IRQ 70 handler
vector irq_71         @ IRQ 71 handler
vector irq_72         @ IRQ 72 handler
vector irq_73         @ IRQ 73 handler
vector irq_74         @ IRQ 74 handler
vector irq_75         @ IRQ 75 handler
vector irq_76         @ IRQ 76 handler
vector irq_77         @ IRQ 77 handler
vector irq_78         @ IRQ 78 handler
vector irq_79         @ IRQ 79 handler
vector irq_80         @ IRQ 80 handler
vector irq_81         @ IRQ 81 handler
vector irq_82         @ IRQ 82 handler
vector irq_83         @ IRQ 83 handler
vector irq_84         @ IRQ 84 handler
vector irq_85         @ IRQ 85 handler
vector irq_86         @ IRQ 86 handler
vector irq_87         @ IRQ 87 handler
vector irq_88         @ IRQ 88 handler
vector irq_89         @ IRQ 89 handler
vector irq_90         @ IRQ 90 handler
vector irq_91         @ IRQ 91 handler
vector irq_92         @ IRQ 92 handler
vector irq_93         @ IRQ 93 handler
vector irq_94         @ IRQ 94 handler
vector irq_95         @ IRQ 95 handler
vector irq_96         @ IRQ 96 handler
vector irq_97         @ IRQ 97 handler
vector irq_98         @ IRQ 98 handler
vector irq_99         @ IRQ 99 handler
vector irq_100        @ IRQ 100 handler
vector irq_101        @ IRQ 101 handler
vector irq_102        @ IRQ 102 handler
vector irq_103        @ IRQ 103 handler
vector irq_104        @ IRQ 104 handler
vector irq_105        @ IRQ 105 handler
vector irq_106        @ IRQ 106 handler
vector irq_107        @ IRQ 107 handler
vector irq_108        @ IRQ 108 handler
vector irq_109        @ IRQ 109 handler
vector irq_110        @ IRQ 110 handler
vector irq_111        @ IRQ 111 handler
vector irq_112        @ IRQ 112 handler
vector irq_113        @ IRQ 113 handler
vector irq_114        @ IRQ 114 handler
vector irq_115        @ IRQ 115 handler
vector irq_116        @ IRQ 116 handler
vector irq_117        @ IRQ 117 handler
vector irq_118        @ IRQ 118 handler
vector irq_119        @ IRQ 119 handler
vector irq_120        @ IRQ 120 handler
vector irq_121        @ IRQ 121 handler
vector irq_122        @ IRQ 122 handler
vector irq_123        @ IRQ 123 handler
vector irq_124        @ IRQ 124 handler
vector irq_125        @ IRQ 125 handler
vector irq_126        @ IRQ 126 handler
vector irq_127        @ IRQ 127 handler
vector irq_128        @ IRQ 128 handler
vector irq_129        @ IRQ 129 handler
vector irq_130        @ IRQ 130 handler
vector irq_131        @ IRQ 131 handler
.rept 108
.long 0               @ IRQ 132-239: reserved
.endr

.global reset
.thumb_func
reset:
    /* set the vector table on our current code */
    adr r1, vectors
    ldr r2, =0xE000ED08   /* VTABLE register in SCB*/
    str r1, [r2]
    /* Clear BSS */
    mov r0, #0
    ldr r1,_bss_start
    ldr r2,_bss_end
bss_loop:
    cmp r1, r2
    it lt
    strlt r0, [r1], #4
    blt bss_loop

    /* Copy initialized data to Internal RAM */
    ldr r0,_ro_end
    ldr r1,_data_start
    ldr r2,_data_end
data_loop:
    ldr r3, [r0], #4
    cmp r1, r2
    it lt
    strlt r3, [r1], #4
    blt data_loop

    /**
     * Set stack pointer
     * already done my Cortex-M hardware but this allows software to jump directly
     * to reset function or to run on other ARM
     */
    ldr r0, =stack_end
    mov sp, r0

    /* jump to C code */
    bl main
    /* we should not return here */
    /* TODO check error code ? */
fini_loop:
    b fini_loop

/* default exception handler */
.thumb_func
default_handler:
    b panic

_bss_start:
    .long __bss_start
_bss_end:
    .long __bss_end
_data_start:
    .long __data_start
_data_end:
    .long __data_end
_ro_end:
    .long __ro_end

/* Dummy functions to avoid linker complaints */
.global __aeabi_unwind_cpp_pr0
.global __aeabi_unwind_cpp_pr1
.global __aeabi_unwind_cpp_pr2
__aeabi_unwind_cpp_pr0:
__aeabi_unwind_cpp_pr1:
__aeabi_unwind_cpp_pr2:
    bx lr

.section .bss

/* Reserve space for system stack */
stack_start:
  .space CONFIG_STACK_SIZE, 0
stack_end:
  .globl stack_end

