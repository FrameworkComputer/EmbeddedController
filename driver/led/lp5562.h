/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI LP5562 LED driver.
 */

#ifndef LP5562_H
#define LP5562_H

#define LP5562_REG_ENABLE	0x00
#define LP5562_REG_OP_MODE	0x01
#define LP5562_REG_B_PWM	0x02
#define LP5562_REG_G_PWM	0x03
#define LP5562_REG_R_PWM	0x04
#define LP5562_REG_B_CURRENT	0x05
#define LP5562_REG_G_CURRENT	0x06
#define LP5562_REG_R_CURRENT	0x07
#define LP5562_REG_CONFIG	0x08
#define LP5562_REG_ENG1_PC	0x09
#define LP5562_REG_ENG2_PC	0x0a
#define LP5562_REG_ENG3_PC	0x0b
#define LP5562_REG_STATUS	0x0c
#define LP5562_REG_RESET	0x0d
#define LP5562_REG_W_PWM	0x0e
#define LP5562_REG_W_CURRENT	0x0f
#define LP5562_REG_LED_MAP	0x70

#define LP5562_REG_ENG_PROG(n)	(0x10 + ((n)-1) * 0x20)

/* Brightness range: 0x00 - 0xff */
#define LP5562_COLOR_NONE	0x000000
#define LP5562_COLOR_RED(b)	(0x010000 * (b))
#define LP5562_COLOR_GREEN(b)	(0x000100 * (b))
#define LP5562_COLOR_BLUE(b)	(0x000001 * (b))

#define LP5562_ENG_SEL_NONE	0x0
#define LP5562_ENG_SEL_1	0x1
#define LP5562_ENG_SEL_2	0x2
#define LP5562_ENG_SEL_3	0x3

#define LP5562_ENG_HOLD		0x0
#define LP5562_ENG_STEP		0x1
#define LP5562_ENG_RUN		0x2

/* Power on and initialize LP5562. */
int lp5562_poweron(void);

/* Power off LP5562. */
int lp5562_poweroff(void);

/*
 * Set LED color.
 * The parameter 'rgb' is in the format 0x00RRGGBB.
 */
int lp5562_set_color(uint32_t rgb);

/* Set lighting engine used by each color */
int lp5562_set_engine(uint8_t r, uint8_t g, uint8_t b);

/* Load lighting engine program */
int lp5562_engine_load(int engine, const uint8_t *program, int size);

/* Control lighting engine execution state */
int lp5562_engine_control(int eng1, int eng2, int eng3);

/* Get engine execution state. Return 0xee on error. */
int lp5562_get_engine_state(int engine);

/* Get current program counter. Return 0xee on error. */
int lp5562_get_pc(int engine);

/* Set program counter */
int lp5562_set_pc(int engine, int val);

#endif /* LP5562_H */
