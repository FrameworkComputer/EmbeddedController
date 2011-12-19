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
.long stack_end       		  @ initial stack pointer
.long reset           		  @ reset handler
vector nmi            		  @ NMI handler
vector hard_fault     		  @ HardFault handler
vector mpu_fault      		  @ MPU fault handler
vector bus_fault      		  @ Bus fault handler
vector usage_fault    		  @ Usage fault handler
.long 0               		  @ reserved
.long 0               		  @ reserved
.long 0               		  @ reserved
.long 0               		  @ reserved
vector svc            		  @ SWI
vector debug          		  @ Debug handler
.long 0               		  @ reserved
vector pendsv         		  @ PendSV handler
vector sys_tick       		  @ SysTick handler
vector irq_LM4_IRQ_GPIOA          @ IRQ   0 handler
vector irq_LM4_IRQ_GPIOB          @ IRQ   1 handler
vector irq_LM4_IRQ_GPIOC          @ IRQ   2 handler
vector irq_LM4_IRQ_GPIOD          @ IRQ   3 handler
vector irq_LM4_IRQ_GPIOE          @ IRQ   4 handler
vector irq_LM4_IRQ_UART0          @ IRQ   5 handler
vector irq_LM4_IRQ_UART1          @ IRQ   6 handler
vector irq_LM4_IRQ_SSI0           @ IRQ   7 handler
vector irq_LM4_IRQ_I2C0           @ IRQ   8 handler
.rept 5
.long 0                           @ IRQ   9-13: reserved
.endr
vector irq_LM4_IRQ_ADC0_SS0       @ IRQ  14 handler
vector irq_LM4_IRQ_ADC0_SS1       @ IRQ  15 handler
vector irq_LM4_IRQ_ADC0_SS2       @ IRQ  16 handler
vector irq_LM4_IRQ_ADC0_SS3       @ IRQ  17 handler
vector irq_LM4_IRQ_WATCHDOG       @ IRQ  18 handler
vector irq_LM4_IRQ_TIMER0A        @ IRQ  19 handler
vector irq_LM4_IRQ_TIMER0B        @ IRQ  20 handler
vector irq_LM4_IRQ_TIMER1A        @ IRQ  21 handler
vector irq_LM4_IRQ_TIMER1B        @ IRQ  22 handler
vector irq_LM4_IRQ_TIMER2A        @ IRQ  23 handler
vector irq_LM4_IRQ_TIMER2B        @ IRQ  24 handler
vector irq_LM4_IRQ_ACMP0          @ IRQ  25 handler
vector irq_LM4_IRQ_ACMP1          @ IRQ  26 handler
vector irq_LM4_IRQ_ACMP2          @ IRQ  27 handler
vector irq_LM4_IRQ_SYSCTRL        @ IRQ  28 handler
vector irq_LM4_IRQ_EEPROM         @ IRQ  29 handler
vector irq_LM4_IRQ_GPIOF          @ IRQ  30 handler
vector irq_LM4_IRQ_GPIOG          @ IRQ  31 handler
vector irq_LM4_IRQ_GPIOH          @ IRQ  32 handler
vector irq_LM4_IRQ_UART2          @ IRQ  33 handler
vector irq_LM4_IRQ_SSI1           @ IRQ  34 handler
vector irq_LM4_IRQ_TIMER3A        @ IRQ  35 handler
vector irq_LM4_IRQ_TIMER3B        @ IRQ  36 handler
vector irq_LM4_IRQ_I2C1           @ IRQ  37 handler
.rept 5
.long 0                           @ IRQ  38-42: reserved
.endr
vector irq_LM4_IRQ_HIBERNATE      @ IRQ  43 handler
.long 0                           @ IRQ  44: reserved
.long 0                           @ IRQ  45: reserved
vector irq_LM4_IRQ_UDMA_SOFTWARE  @ IRQ  46 handler
vector irq_LM4_IRQ_UDMA_ERROR     @ IRQ  47 handler
vector irq_LM4_IRQ_ADC1_SS0       @ IRQ  48 handler
vector irq_LM4_IRQ_ADC1_SS1       @ IRQ  49 handler
vector irq_LM4_IRQ_ADC1_SS2       @ IRQ  50 handler
vector irq_LM4_IRQ_ADC1_SS3       @ IRQ  51 handler
.long 0                           @ IRQ  52: reserved
.long 0                           @ IRQ  53: reserved
vector irq_LM4_IRQ_GPIOJ          @ IRQ  54 handler
vector irq_LM4_IRQ_GPIOK          @ IRQ  55 handler
vector irq_LM4_IRQ_GPIOL          @ IRQ  56 handler
vector irq_LM4_IRQ_SSI2           @ IRQ  57 handler
vector irq_LM4_IRQ_SSI3           @ IRQ  58 handler
vector irq_LM4_IRQ_UART3          @ IRQ  59 handler
vector irq_LM4_IRQ_UART4          @ IRQ  60 handler
vector irq_LM4_IRQ_UART5          @ IRQ  61 handler
vector irq_LM4_IRQ_UART6          @ IRQ  62 handler
vector irq_LM4_IRQ_UART7          @ IRQ  63 handler
.rept 4
.long 0                           @ IRQ  64-67: reserved
.endr
vector irq_LM4_IRQ_I2C2           @ IRQ  68 handler
vector irq_LM4_IRQ_I2C3           @ IRQ  69 handler
vector irq_LM4_IRQ_TIMER4A        @ IRQ  70 handler
vector irq_LM4_IRQ_TIMER4B        @ IRQ  71 handler
.rept 20
.long 0                           @ IRQ  72-91: reserved
.endr
vector irq_LM4_IRQ_TIMER5A        @ IRQ  92 handler
vector irq_LM4_IRQ_TIMER5B        @ IRQ  93 handler
vector irq_LM4_IRQ_TIMERW0A       @ IRQ  94 handler
vector irq_LM4_IRQ_TIMERW0B       @ IRQ  95 handler
vector irq_LM4_IRQ_TIMERW1A       @ IRQ  96 handler
vector irq_LM4_IRQ_TIMERW1B       @ IRQ  97 handler
vector irq_LM4_IRQ_TIMERW2A       @ IRQ  98 handler
vector irq_LM4_IRQ_TIMERW2B       @ IRQ  99 handler
vector irq_LM4_IRQ_TIMERW3A       @ IRQ 100 handler
vector irq_LM4_IRQ_TIMERW3B       @ IRQ 101 handler
vector irq_LM4_IRQ_TIMERW4A       @ IRQ 102 handler
vector irq_LM4_IRQ_TIMERW4B       @ IRQ 103 handler
vector irq_LM4_IRQ_TIMERW5A       @ IRQ 104 handler
vector irq_LM4_IRQ_TIMERW5B       @ IRQ 105 handler
vector irq_LM4_IRQ_SYS_EXCEPTION  @ IRQ 106 handler
vector irq_LM4_IRQ_SYS_PECI       @ IRQ 107 handler
vector irq_LM4_IRQ_LPC            @ IRQ 108 handler
vector irq_LM4_IRQ_I2C4           @ IRQ 109 handler
vector irq_LM4_IRQ_I2C5           @ IRQ 110 handler
vector irq_LM4_IRQ_GPIOM          @ IRQ 111 handler
vector irq_LM4_IRQ_GPION          @ IRQ 112 handler
.long 0                           @ IRQ 113: reserved
vector irq_LM4_IRQ_FAN            @ IRQ 114 handler
.long 0                           @ IRQ 115: reserved
vector irq_LM4_IRQ_GPIOP          @ IRQ 116 handler
vector irq_LM4_IRQ_GPIOP1         @ IRQ 117 handler
vector irq_LM4_IRQ_GPIOP2         @ IRQ 118 handler
vector irq_LM4_IRQ_GPIOP3         @ IRQ 119 handler
vector irq_LM4_IRQ_GPIOP4         @ IRQ 120 handler
vector irq_LM4_IRQ_GPIOP5         @ IRQ 121 handler
vector irq_LM4_IRQ_GPIOP6         @ IRQ 122 handler
vector irq_LM4_IRQ_GPIOP7         @ IRQ 123 handler
vector irq_LM4_IRQ_GPIOQ          @ IRQ 124 handler
vector irq_LM4_IRQ_GPIOQ1         @ IRQ 125 handler
vector irq_LM4_IRQ_GPIOQ2         @ IRQ 126 handler
vector irq_LM4_IRQ_GPIOQ3         @ IRQ 127 handler
vector irq_LM4_IRQ_GPIOQ4         @ IRQ 128 handler
vector irq_LM4_IRQ_GPIOQ5         @ IRQ 129 handler
vector irq_LM4_IRQ_GPIOQ6         @ IRQ 130 handler
vector irq_LM4_IRQ_GPIOQ7         @ IRQ 131 handler
.rept 108
.long 0                           @ IRQ 132-239: reserved
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
     * already done my Cortex-M hardware but this allows software to
     * jump directly to reset function or to run on other ARM
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

