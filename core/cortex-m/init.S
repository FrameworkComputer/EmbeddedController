/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cortex-M CPU initialization
 */

#include "config.h"

.section .text.vecttable

.macro vector name
.long \name\()_handler
.weak \name\()_handler
.set \name\()_handler, default_handler
.endm

.macro vector_irq number
.if \number < CONFIG_IRQ_COUNT
vector irq_\()\number
.endif
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
vector_irq 0                      @ IRQ   0 handler
vector_irq 1                      @ IRQ   1 handler
vector_irq 2                      @ IRQ   2 handler
vector_irq 3                      @ IRQ   3 handler
vector_irq 4                      @ IRQ   4 handler
vector_irq 5                      @ IRQ   5 handler
vector_irq 6                      @ IRQ   6 handler
vector_irq 7                      @ IRQ   7 handler
vector_irq 8                      @ IRQ   8 handler
vector_irq 9                      @ IRQ   9 handler
vector_irq 10                     @ IRQ  10 handler
vector_irq 11                     @ IRQ  11 handler
vector_irq 12                     @ IRQ  12 handler
vector_irq 13                     @ IRQ  13 handler
vector_irq 14                     @ IRQ  14 handler
vector_irq 15                     @ IRQ  15 handler
vector_irq 16                     @ IRQ  16 handler
vector_irq 17                     @ IRQ  17 handler
vector_irq 18                     @ IRQ  18 handler
vector_irq 19                     @ IRQ  19 handler
vector_irq 20                     @ IRQ  20 handler
vector_irq 21                     @ IRQ  21 handler
vector_irq 22                     @ IRQ  22 handler
vector_irq 23                     @ IRQ  23 handler
vector_irq 24                     @ IRQ  24 handler
vector_irq 25                     @ IRQ  25 handler
vector_irq 26                     @ IRQ  26 handler
vector_irq 27                     @ IRQ  27 handler
vector_irq 28                     @ IRQ  28 handler
vector_irq 29                     @ IRQ  29 handler
vector_irq 30                     @ IRQ  30 handler
vector_irq 31                     @ IRQ  31 handler
vector_irq 32                     @ IRQ  32 handler
vector_irq 33                     @ IRQ  33 handler
vector_irq 34                     @ IRQ  34 handler
vector_irq 35                     @ IRQ  35 handler
vector_irq 36                     @ IRQ  36 handler
vector_irq 37                     @ IRQ  37 handler
vector_irq 38                     @ IRQ  38 handler
vector_irq 39                     @ IRQ  39 handler
vector_irq 40                     @ IRQ  40 handler
vector_irq 41                     @ IRQ  41 handler
vector_irq 42                     @ IRQ  42 handler
vector_irq 43                     @ IRQ  43 handler
vector_irq 44                     @ IRQ  44 handler
vector_irq 45                     @ IRQ  45 handler
vector_irq 46                     @ IRQ  46 handler
vector_irq 47                     @ IRQ  47 handler
vector_irq 48                     @ IRQ  48 handler
vector_irq 49                     @ IRQ  49 handler
vector_irq 50                     @ IRQ  50 handler
vector_irq 51                     @ IRQ  51 handler
vector_irq 52                     @ IRQ  52 handler
vector_irq 53                     @ IRQ  53 handler
vector_irq 54                     @ IRQ  54 handler
vector_irq 55                     @ IRQ  55 handler
vector_irq 56                     @ IRQ  56 handler
vector_irq 57                     @ IRQ  57 handler
vector_irq 58                     @ IRQ  58 handler
vector_irq 59                     @ IRQ  59 handler
vector_irq 60                     @ IRQ  60 handler
vector_irq 61                     @ IRQ  61 handler
vector_irq 62                     @ IRQ  62 handler
vector_irq 63                     @ IRQ  63 handler
vector_irq 64                     @ IRQ  64 handler
vector_irq 65                     @ IRQ  65 handler
vector_irq 66                     @ IRQ  66 handler
vector_irq 67                     @ IRQ  67 handler
vector_irq 68                     @ IRQ  68 handler
vector_irq 69                     @ IRQ  69 handler
vector_irq 70                     @ IRQ  70 handler
vector_irq 71                     @ IRQ  71 handler
vector_irq 72                     @ IRQ  72 handler
vector_irq 73                     @ IRQ  73 handler
vector_irq 74                     @ IRQ  74 handler
vector_irq 75                     @ IRQ  75 handler
vector_irq 76                     @ IRQ  76 handler
vector_irq 77                     @ IRQ  77 handler
vector_irq 78                     @ IRQ  78 handler
vector_irq 79                     @ IRQ  79 handler
vector_irq 80                     @ IRQ  80 handler
vector_irq 81                     @ IRQ  81 handler
vector_irq 82                     @ IRQ  82 handler
vector_irq 83                     @ IRQ  83 handler
vector_irq 84                     @ IRQ  84 handler
vector_irq 85                     @ IRQ  85 handler
vector_irq 86                     @ IRQ  86 handler
vector_irq 87                     @ IRQ  87 handler
vector_irq 88                     @ IRQ  88 handler
vector_irq 89                     @ IRQ  89 handler
vector_irq 90                     @ IRQ  90 handler
vector_irq 91                     @ IRQ  91 handler
vector_irq 92                     @ IRQ  92 handler
vector_irq 93                     @ IRQ  93 handler
vector_irq 94                     @ IRQ  94 handler
vector_irq 95                     @ IRQ  95 handler
vector_irq 96                     @ IRQ  96 handler
vector_irq 97                     @ IRQ  97 handler
vector_irq 98                     @ IRQ  98 handler
vector_irq 99                     @ IRQ  99 handler
vector_irq 100                    @ IRQ 100 handler
vector_irq 101                    @ IRQ 101 handler
vector_irq 102                    @ IRQ 102 handler
vector_irq 103                    @ IRQ 103 handler
vector_irq 104                    @ IRQ 104 handler
vector_irq 105                    @ IRQ 105 handler
vector_irq 106                    @ IRQ 106 handler
vector_irq 107                    @ IRQ 107 handler
vector_irq 108                    @ IRQ 108 handler
vector_irq 109                    @ IRQ 109 handler
vector_irq 110                    @ IRQ 110 handler
vector_irq 111                    @ IRQ 111 handler
vector_irq 112                    @ IRQ 112 handler
vector_irq 113                    @ IRQ 113 handler
vector_irq 114                    @ IRQ 114 handler
vector_irq 115                    @ IRQ 115 handler
vector_irq 116                    @ IRQ 116 handler
vector_irq 117                    @ IRQ 117 handler
vector_irq 118                    @ IRQ 118 handler
vector_irq 119                    @ IRQ 119 handler
vector_irq 120                    @ IRQ 120 handler
vector_irq 121                    @ IRQ 121 handler
vector_irq 122                    @ IRQ 122 handler
vector_irq 123                    @ IRQ 123 handler
vector_irq 124                    @ IRQ 124 handler
vector_irq 125                    @ IRQ 125 handler
vector_irq 126                    @ IRQ 126 handler
vector_irq 127                    @ IRQ 127 handler
vector_irq 128                    @ IRQ 128 handler
vector_irq 129                    @ IRQ 129 handler
vector_irq 130                    @ IRQ 130 handler
vector_irq 131                    @ IRQ 131 handler
vector_irq 132                    @ IRQ 132 handler
vector_irq 133                    @ IRQ 133 handler
vector_irq 134                    @ IRQ 134 handler
vector_irq 135                    @ IRQ 135 handler
vector_irq 136                    @ IRQ 136 handler
vector_irq 137                    @ IRQ 137 handler
vector_irq 138                    @ IRQ 138 handler
vector_irq 139                    @ IRQ 139 handler
vector_irq 140                    @ IRQ 140 handler
vector_irq 141                    @ IRQ 141 handler
vector_irq 142                    @ IRQ 142 handler
vector_irq 143                    @ IRQ 143 handler
vector_irq 144                    @ IRQ 144 handler
vector_irq 145                    @ IRQ 145 handler
vector_irq 146                    @ IRQ 146 handler
vector_irq 147                    @ IRQ 147 handler
vector_irq 148                    @ IRQ 148 handler
vector_irq 149                    @ IRQ 149 handler
vector_irq 150                    @ IRQ 150 handler
vector_irq 151                    @ IRQ 151 handler
vector_irq 152                    @ IRQ 152 handler
vector_irq 153                    @ IRQ 153 handler
vector_irq 154                    @ IRQ 154 handler
vector_irq 155                    @ IRQ 155 handler
vector_irq 156                    @ IRQ 156 handler
vector_irq 157                    @ IRQ 157 handler
vector_irq 158                    @ IRQ 158 handler
vector_irq 159                    @ IRQ 159 handler
vector_irq 160                    @ IRQ 160 handler
vector_irq 161                    @ IRQ 161 handler
vector_irq 162                    @ IRQ 162 handler
vector_irq 163                    @ IRQ 163 handler
vector_irq 164                    @ IRQ 164 handler
vector_irq 165                    @ IRQ 165 handler
vector_irq 166                    @ IRQ 166 handler
vector_irq 167                    @ IRQ 167 handler
vector_irq 168                    @ IRQ 168 handler
vector_irq 169                    @ IRQ 169 handler
vector_irq 170                    @ IRQ 170 handler
vector_irq 171                    @ IRQ 171 handler
vector_irq 172                    @ IRQ 172 handler
vector_irq 173                    @ IRQ 173 handler
vector_irq 174                    @ IRQ 174 handler
vector_irq 175                    @ IRQ 175 handler
vector_irq 176                    @ IRQ 176 handler
vector_irq 177                    @ IRQ 177 handler
vector_irq 178                    @ IRQ 178 handler
vector_irq 179                    @ IRQ 179 handler
vector_irq 180                    @ IRQ 180 handler
vector_irq 181                    @ IRQ 181 handler
vector_irq 182                    @ IRQ 182 handler
vector_irq 183                    @ IRQ 183 handler
vector_irq 184                    @ IRQ 184 handler
vector_irq 185                    @ IRQ 185 handler
vector_irq 186                    @ IRQ 186 handler
vector_irq 187                    @ IRQ 187 handler
vector_irq 188                    @ IRQ 188 handler
vector_irq 189                    @ IRQ 189 handler
vector_irq 190                    @ IRQ 190 handler
vector_irq 191                    @ IRQ 191 handler
vector_irq 192                    @ IRQ 192 handler
vector_irq 193                    @ IRQ 193 handler
vector_irq 194                    @ IRQ 194 handler
vector_irq 195                    @ IRQ 195 handler
vector_irq 196                    @ IRQ 196 handler
vector_irq 197                    @ IRQ 197 handler
vector_irq 198                    @ IRQ 198 handler
vector_irq 199                    @ IRQ 199 handler
vector_irq 200                    @ IRQ 200 handler
vector_irq 201                    @ IRQ 201 handler
vector_irq 202                    @ IRQ 202 handler
vector_irq 203                    @ IRQ 203 handler
vector_irq 204                    @ IRQ 204 handler
vector_irq 205                    @ IRQ 205 handler
vector_irq 206                    @ IRQ 206 handler
vector_irq 207                    @ IRQ 207 handler
vector_irq 208                    @ IRQ 208 handler
vector_irq 209                    @ IRQ 209 handler
vector_irq 210                    @ IRQ 210 handler
vector_irq 211                    @ IRQ 211 handler
vector_irq 212                    @ IRQ 212 handler
vector_irq 213                    @ IRQ 213 handler
vector_irq 214                    @ IRQ 214 handler
vector_irq 215                    @ IRQ 215 handler
vector_irq 216                    @ IRQ 216 handler
vector_irq 217                    @ IRQ 217 handler
vector_irq 218                    @ IRQ 218 handler
vector_irq 219                    @ IRQ 219 handler
vector_irq 220                    @ IRQ 220 handler
vector_irq 221                    @ IRQ 221 handler
vector_irq 222                    @ IRQ 222 handler
vector_irq 223                    @ IRQ 223 handler
vector_irq 224                    @ IRQ 224 handler
vector_irq 225                    @ IRQ 225 handler
vector_irq 226                    @ IRQ 226 handler
vector_irq 227                    @ IRQ 227 handler
vector_irq 228                    @ IRQ 228 handler
vector_irq 229                    @ IRQ 229 handler
vector_irq 230                    @ IRQ 230 handler
vector_irq 231                    @ IRQ 231 handler
vector_irq 232                    @ IRQ 232 handler
vector_irq 233                    @ IRQ 233 handler
vector_irq 234                    @ IRQ 234 handler
vector_irq 235                    @ IRQ 235 handler
vector_irq 236                    @ IRQ 236 handler
vector_irq 237                    @ IRQ 237 handler
vector_irq 238                    @ IRQ 238 handler
vector_irq 239                    @ IRQ 239 handler
vector_irq 240                    @ IRQ 240 handler
vector_irq 241                    @ IRQ 241 handler
vector_irq 242                    @ IRQ 242 handler
vector_irq 243                    @ IRQ 243 handler
vector_irq 244                    @ IRQ 244 handler
vector_irq 245                    @ IRQ 245 handler
vector_irq 246                    @ IRQ 246 handler
vector_irq 247                    @ IRQ 247 handler
vector_irq 248                    @ IRQ 248 handler
vector_irq 249                    @ IRQ 249 handler
vector_irq 250                    @ IRQ 250 handler
vector_irq 251                    @ IRQ 251 handler
vector_irq 252                    @ IRQ 252 handler
vector_irq 253                    @ IRQ 253 handler
vector_irq 254                    @ IRQ 254 handler

.text
.syntax unified
.code 16

.global reset
.thumb_func
reset:
    /* Ensure we're in privileged mode with main stack.
     * Necessary if we've jumped directly here from another image
     * after task_start(). */
#ifdef CONFIG_FPU
    mov r0, #(1 << 2)    @ priv. mode / main stack / floating point on
#else
    mov r0, #0           @ priv. mode / main stack / no floating point
#endif
    msr control, r0
    isb                  @ ensure the write is done
    /* Set the vector table on our current code */
    ldr r1, =vectors
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

#ifndef COMPILE_FOR_RAM
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
#endif

    /**
     * Set stack pointer
     * already done my Cortex-M hardware but this allows software to
     * jump directly to reset function or to run on other ARM
     */
    ldr r0, =stack_end
    mov sp, r0

#ifdef CONFIG_FPU
    /* Enable FPU */
    /* CPACR is located at address 0xE000ED88 */
    ldr r0, =0xE000ED88
    /* Read CPACR */
    ldr r1, [r0]
    /* Set bits 20-23 to enable CP10 and CP11 coprocessors */
    orr r1, r1, #(0xF << 20)
    /* Write back the modified value to the CPACR */
    str r1, [r0] /* wait for store to complete */
    dsb
    /* reset pipeline now the FPU is enabled */
    isb
#endif /* CONFIG_FPU */

    /* jump to C code */
    bl main
    /* we should not return here */
    /* TODO check error code ? */
fini_loop:
    b fini_loop

/* default exception handler */
.thumb_func
default_handler:
    b exception_panic

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

/* Reserve space for system stack */
.section .bss.system_stack
stack_start:
  .space CONFIG_STACK_SIZE, 0
stack_end:
  .global stack_end

