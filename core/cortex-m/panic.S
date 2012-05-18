/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fatal exception handling and debug tracing
 */

#include "config.h"

.text

.syntax unified
.code 16

.macro hex_reg r, offset                      @ prepare to build
    add r1, r3, #\offset                      @ .. hexadecimal string
    mov r0, \r                                @ .. from the reg value
    bl buildhex
.endm

/* fatal exception handler */
.global panic
.thumb_func
panic:
#ifndef CONFIG_DEBUG
    b EcSystemReset                           @ Reboot the system
#else /* CONFIG_DEBUG */
    /* check that the exception stack pointer is valid */
    ldr r0, =CONFIG_RAM_BASE                  @ start of RAM
    ldr r1, =CONFIG_RAM_BASE+CONFIG_RAM_SIZE  @ end of RAM
    mrs r12, psp                              @ process stack pointer
    cmp r12, r0                               @ is sp >= RAM start ?
    it  ge
    cmpge r1, r12                             @ is sp < RAM end ?
    blt panic_print                           @ no => no values to read
    /* output registers value */
    ldr r3, =msg_excep                        @ pointer to the text buffer
    mrs r2, ipsr                              @ get exception num from IPSR
    bfc r2, #9, #23                           @ the exception is the 3 LSB
    hex_reg r2, 18                            @ prepare hexa for excep number
    hex_reg r4, 119                           @ prepare hexa for R4
    hex_reg r5, 132                           @ prepare hexa for R5
    hex_reg r6, 145                           @ prepare hexa for R6
    hex_reg r7, 156                           @ prepare hexa for R7
    hex_reg r8, 171                           @ prepare hexa for R8
    hex_reg r9, 184                           @ prepare hexa for R9
    hex_reg r10, 197                          @ prepare hexa for R10
    hex_reg r11, 210                          @ prepare hexa for R11
    ldmia r12!, {r4-r11}                      @ load saved r0-r3,r12,lr,pc,psr
    hex_reg r4, 66                            @ prepare hexa for R0
    hex_reg r5, 79                            @ prepare hexa for R1
    hex_reg r6, 92                            @ prepare hexa for R2
    hex_reg r7, 105                           @ prepare hexa for R3
    hex_reg r8, 225                           @ prepare hexa for R12
    hex_reg r12, 238                          @ prepare hexa for SP
    hex_reg r9, 251                           @ prepare hexa for LR
    hex_reg r10, 264                          @ prepare hexa for PC
    hex_reg r11, 40                           @ prepare hexa for xPSR
    /* print exception trace */
panic_print:
    ldr r0, =msg_excep                        @ pointer to the text buffer
    bl emergency_puts                         @ print the banner
    mov r0, #0                                @ Soft boot
    b system_reset                            @ Reboot the system

/* Helpers for exception trace */
/* print a string on the UART
 * r0: asciiZ string pointer
 */
emergency_puts:
    ldr r1, =CONFIG_UART_ADDRESS              @ UART base address
1:
    ldrb r3, [r0], #1                         @ read one character
    cmp r3, #0                                @ end of the string ?
    beq 3f                                    @ if yes, return
2:  /* putchar */
    ldr r2, [r1, #CONFIG_UART_SR_OFFSET]      @ read UART status
    tst r2, #CONFIG_UART_SR_TXEMPTY           @ free space on TX ?
    beq 2b                                    @ if no, wait
    str r3, [r1, #CONFIG_UART_DR_OFFSET]      @ send character to UART DR
    b 1b                                      @ goto next character
3:  /* return */
    bx lr

/* write a number in hexadecimal in a text buffer
 * r0: number to print
 * r1: pointer to *end* of the buffer (filled with '0')
 */
buildhex:
    cmp r0, #0
    it  eq
    bxeq lr
    and r2, r0, #0xf
    cmp r2, #10
    ite  lt
    addlt r2, #'0'
    addge r2, #'A'-10
    strb r2, [r1],#-1
    lsr r0, #4
    b buildhex

.data
msg_excep: .ascii "\r\n=== EXCEPTION: 00 ====== xPSR: 00000000 ===========\r\n"
msg_reg0:  .ascii "R0 :00000000 R1 :00000000 R2 :00000000 R3 :00000000\r\n"
msg_reg1:  .ascii "R4 :00000000 R5 :00000000 R6 :00000000 R7 :00000000\r\n"
msg_reg2:  .ascii "R8 :00000000 R9 :00000000 R10:00000000 R11:00000000\r\n"
msg_reg3:  .ascii "R12:00000000 SP :00000000 LR :00000000 PC :00000000\r\n\0"
.align 4
#endif /* CONFIG_DEBUG */
