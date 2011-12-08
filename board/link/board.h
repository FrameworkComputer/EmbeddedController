/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* configuration for Link mainboard */

#ifndef __BOARD_H
#define __BOARD_H

/* 66.667 Mhz clock frequency */
#define CPU_CLOCK  66666667

/* Fan PWM channels */
#define FAN_CH_CPU       0  /* CPU fan */
#define FAN_CH_KBLIGHT   1  /* Keyboard backlight */
#define FAN_CH_POWER_LED 5  /* Power adapter LED */

/* TODO: these should really only be used inside lpc.c; once they are, remove
 * from board header files. */
/* LPC channels */
#define LPC_CH_KERNEL   0  /* Kernel commands */
#define LPC_CH_PORT80   1  /* Port 80 debug output */
#define LPC_CH_CMD_DATA 2  /* Data for kernel/user-mode commands */
#define LPC_CH_KEYBOARD 3  /* 8042 keyboard emulation */
#define LPC_CH_USER     4  /* User-mode commands */
#define LPC_CH_COMX     7  /* UART emulation */
/* LPC pool offsets */
#define LPC_POOL_OFFS_KERNEL     0  /* Kernel commands - 0=in, 1=out */
#define LPC_POOL_OFFS_PORT80     4  /* Port 80 - 4=in, 5=out */
#define LPC_POOL_OFFS_COMX       8  /* UART emulation range - 8-15 */
#define LPC_POOL_OFFS_KEYBOARD  16  /* Keyboard - 16=in, 17=out */
#define LPC_POOL_OFFS_USER      20  /* User commands - 20=in, 21=out */
#define LPC_POOL_OFFS_CMD_DATA 512  /* Data range for commands - 512-1023 */
/* LPC pool data pointers */
#define LPC_POOL_KERNEL   (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_KERNEL)
#define LPC_POOL_PORT80   (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_PORT80)
#define LPC_POOL_COMX     (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_COMX)
#define LPC_POOL_KEYBOARD (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_KEYBOARD)
#define LPC_POOL_CMD_DATA (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_CMD_DATA)
#define LPC_POOL_USER     (LM4_LPC_LPCPOOL + LPC_POOL_OFFS_USER)

/* ADC inputs */
/* TODO: assign real ADC inputs */
#define ADC_IN_POT 0  /* Turn POT on badger board */

/* I2C ports */
#define I2C_PORT_BATTERY 0
#define I2C_PORT_CHARGER 1
#define I2C_PORT_THERMAL 5
/* I2C port speeds in kbps */
#define I2C_SPEED_BATTERY 100
#define I2C_SPEED_CHARGER 100
#define I2C_SPEED_THERMAL 400  /* TODO: TMP007 supports 3.4Mbps
				  operation; use faster speed? */

void configure_board(void);

#endif /* __BOARD_H */
