/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This defines a list of lightbar opcodes for programmable sequences.
 */

/* 	   NAME			OPERAND BYTES	MNEMONIC*/
#define LIGHTBAR_OPCODE_TABLE					\
	OP(ON,			0,		"on"		)\
	OP(OFF,			0,		"off"		)\
	OP(JUMP,		1,		"jump"		)\
	OP(JUMP_BATTERY,	2,		"jbat"		)\
	OP(JUMP_IF_CHARGING,	1,		"jcharge"	)\
	OP(SET_WAIT_DELAY,	4,		"delay.w"	)\
	OP(SET_RAMP_DELAY,	4,		"delay.r"	)\
	OP(WAIT,		0,		"wait"		)\
	OP(SET_BRIGHTNESS,	1,		"bright"	)\
	OP(SET_COLOR_SINGLE,	2,		"set.1"		)\
	OP(SET_COLOR_RGB,	4,		"set.rgb"	)\
	OP(GET_COLORS,		0,		"get"		)\
	OP(SWAP_COLORS,		0,		"swap"		)\
	OP(RAMP_ONCE,		0,		"ramp.1"	)\
	OP(CYCLE_ONCE,		0,		"cycle.1"	)\
	OP(CYCLE,		0,		"cycle"		)\
	OP(HALT,		0,		"halt"		)
