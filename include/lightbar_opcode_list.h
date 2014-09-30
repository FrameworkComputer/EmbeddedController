/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This defines a list of lightbar opcodes for programmable sequences.
 */
#define LIGHTBAR_OPCODE_TABLE	\
	OP(JUMP),		\
	OP(DELAY),		\
	OP(SET_BRIGHTNESS),	\
	OP(SET_COLOR),	\
	OP(SET_DELAY_TIME),	\
	OP(RAMP_ONCE),	\
	OP(CYCLE_ONCE),	\
	OP(CYCLE),	\
	OP(HALT),
