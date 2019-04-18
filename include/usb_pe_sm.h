/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Policy Engine module */

#ifndef __CROS_EC_USB_PE_H
#define __CROS_EC_USB_PE_H

#include "usb_sm.h"

enum pe_error {
	ERR_RCH_CHUNKED,
	ERR_RCH_MSG_REC,
	ERR_TCH_CHUNKED,
	ERR_TCH_XMIT,
};

/*
 * PE_OBJ is a convenience macro to access struct sm_obj, which
 * must be the first member of struct policy_engine.
 */
#define PE_OBJ(port)   (SM_OBJ(pe[port]))

/**
 * Initialize the Policy Engine State Machine
 *
 * @param port USB-C port number
 */
void pe_init(int port);

/**
 * Runs the Policy Engine State Machine
 *
 * @param port USB-C port number
 * @param evt  system event, ie: PD_EVENT_RX
 * @param en   0 to disable the machine, 1 to enable the machine
 */
void usbc_policy_engine(int port, int evt, int en);

/**
 * Informs the Policy Engine that a message was successfully sent
 *
 * @param port USB-C port number
 */
void pe_message_sent(int port);

/**
 * Informs the Policy Engine of an error.
 *
 * @param port USB-C port number
 * @parm  e    error
 */
void pe_report_error(int port, enum pe_error e);

/**
 * Informs the Policy Engine that a message has been received
 *
 * @param port USB-C port number
 * @parm  e    error
 */
void pe_pass_up_message(int port);

/**
 * Informs the Policy Engine that a hard reset was received.
 *
 * @param port USB-C port number
 */
void pe_got_hard_reset(int port);

/**
 * Informs the Policy Engine that a soft reset was received.
 *
 * @param port USB-C port number
 */
void pe_got_soft_reset(int port);

/**
 * Informs the Policy Engine that a hard reset was sent.
 *
 * @param port USB-C port number
 */
void pe_hard_reset_sent(int port);

#endif /* __CROS_EC_USB_PE_H */

