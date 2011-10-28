/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * i8042.h -- defines the interface between EC core and the EC lib, which
 * talks to the LPC driver (on the EC side) peering to the keyboard driver
 * (on the host side).
 *
 * The EC lib implements this interface.
 */

#ifndef __HOST_INTERFACE_I8042_H
#define __HOST_INTERFACE_I8042_H

#include "cros_ec/include/ec_common.h"


/* Keyboard command definition. Modified from Linux kernel atkbd.c file. */
#define EC_I8042_CMD_SETLEDS       0xed
#define EC_I8042_CMD_GSCANSET      0xf0
#define EC_I8042_CMD_SSCANSET      0xf0
#define EC_I8042_CMD_GETID         0xf2
#define EC_I8042_CMD_SETREP        0xf3
#define EC_I8042_CMD_ENABLE        0xf4
#define EC_I8042_CMD_RESET_DIS     0xf5
#define EC_I8042_CMD_RESET_DEF     0xf6
#define EC_I8042_CMD_SETALL_MB     0xf8
#define EC_I8042_CMD_SETALL_MBR    0xfa
#define EC_I8042_CMD_RESET_BAT     0xff
#define EC_I8042_CMD_RESEND        0xfe
#define EC_I8042_CMD_EX_ENABLE     0xea
#define EC_I8042_CMD_EX_SETLEDS    0xeb
#define EC_I8042_CMD_OK_GETID      0xe8

#define EC_I8042_RET_ACK           0xfa
#define EC_I8042_RET_NAK           0xfe
#define EC_I8042_RET_BAT           0xaa
#define EC_I8042_RET_EMUL0         0xe0
#define EC_I8042_RET_EMUL1         0xe1
#define EC_I8042_RET_RELEASE       0xf0
#define EC_I8042_RET_HANJA         0xf1
#define EC_I8042_RET_HANGEUL       0xf2
#define EC_I8042_RET_ERR           0xff


/*
 * Register the i8042 callback to EC lib.
 *
 * The callback function would return an integer to indicate how many bytes
 * contain in output (max len is MAX_I8042_OUTPUT_LEN defined below).
 * Then the EC lib would output those bytes via port 0x60 one-by-one.
 *
 * Registering a NULL pointer can remove any registered callback.
 */
typedef int (*EcI8042Callback) (
    uint8_t command,
    uint8_t data,
    uint8_t *output);

EcError EcI8042RegisterCallback(EcI8042Callback callback);
#define MAX_I8042_OUTPUT_LEN 4


/* Send the scan code to the host. The EC lib will push the scan code bytes
 * to host via port 0x60 and assert the IBF flag to trigger an interrupt.
 * The EC lib must queue them if the host cannot read the previous byte away
 * in time.
 *
 * Return:
 *   EC_ERROR_BUFFER_FULL -- the queue to host is full. Try again?
 */
EcError EcI8042SendScanCode(int len, uint8_t *scan_code);


#endif  /* __HOST_INTERFACE_I8042_H */
