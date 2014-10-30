/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __INJECTOR_H
#define __INJECTOR_H

/*
 * Finite state machine definition for sending complex sequences
 *
 * the 32-bit commands have the following definition :
 *    [31:28]   [27:0]
 *    Command    arg
 *   INJ_CMD_x
 *                  [27:16]     [15:0]
 *                   arg12       arg0
 *              [27:24] [23:16] [15:0]
 *               arg2     arg1   arg0
 */

/* Macros to extract values from FSM command words */
#define INJ_CMD(w) ((w) >> 28)
#define INJ_ARG(w) ((w) & 0x0FFFFFFF)
#define INJ_ARG0(w) ((w) & 0x0000FFFF)
#define INJ_ARG1(w) (((w) >> 16) & 0xFF)
#define INJ_ARG2(w) (((w) >> 24) & 0xF)
#define INJ_ARG12(w) (((w) >> 16) & 0xFFF)

enum inj_cmd {
	INJ_CMD_END   = 0x0, /* stop the FSM */
	INJ_CMD_SEND  = 0x1, /* Send message on CCx */
			     /* arg0: header arg1/2:payload index/count  */
	INJ_CMD_WAVE  = 0x2, /* Send arbitrary waveform */
			     /* stored at index arg1 of len arg0 */
	INJ_CMD_HRST  = 0x3, /* Send Hard Reset on CCx */
	INJ_CMD_WAIT  = 0x4, /* Wait for arg12 edges if arg12 != 0 */
			     /* and timeout after arg0 ms */
	INJ_CMD_GET   = 0x5, /* Get parameter arg1 (INJ_GET_x) at index arg0 */
	INJ_CMD_SET   = 0x6, /* Set parameter arg1 (INJ_SET_x) with arg0 */
	INJ_CMD_JUMP  = 0x8, /* Jump to index (as arg0) */
	INJ_CMD_NOP   = 0xF, /* No-Operation */
};

enum inj_set {
	INJ_SET_RESISTOR1  = 0, /* CC1 resistor as arg0 (INJ_RES_x) */
	INJ_SET_RESISTOR2  = 1, /* CC2 resistor as arg0 (INJ_RES_x) */
	INJ_SET_RECORD     = 2, /* Recording on/off */
	INJ_SET_TX_SPEED   = 3, /* TX frequency is arg0 kHz */
	INJ_SET_RX_THRESH  = 4, /* RX voltage threshold is arg0 mV */
	INJ_SET_POLARITY   = 5, /* Polarity for other operations (INJ_POL_CC) */
};

enum inj_get {
	INJ_GET_CC       = 0,  /* CC1/CC2 voltages in mV */
	INJ_GET_VBUS     = 1,  /* VBUS voltage in mV and current in mA */
	INJ_GET_VCONN    = 2, /* VCONN voltage in mV and current in mA */
	INJ_GET_POLARITY = 3, /* Current polarity (INJ_POL_CC) */
};

enum inj_res {
	INJ_RES_NONE  = 0,
	INJ_RES_RA    = 1,
	INJ_RES_RD    = 2,
	INJ_RES_RPUSB = 3,
	INJ_RES_RP1A5 = 4,
	INJ_RES_RP3A0 = 5,
};

enum inj_pol {
	INJ_POL_CC1 = 0,
	INJ_POL_CC2 = 1,
	INJ_POL_AUTO = 0xffff,
};

/* Number of words in the FSM command/data buffer  */
#define INJ_CMD_COUNT 128

#endif /* __INJECTOR_H */
