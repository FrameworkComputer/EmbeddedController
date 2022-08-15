/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ATKBD keyboard protocol constants.
 *
 * See the IBL PC AT Technical Reference Manual Section 4.
 * i.e., 6183355_PC_AT_Technical_Reference_Mar86.pdf
 *
 * https://archive.org/details/bitsavers_ibmpcat618ferenceMar86_25829277/page/n151/mode/2up
 */

#ifndef __CROS_EC_ATKBD_PROTOCOL_H
#define __CROS_EC_ATKBD_PROTOCOL_H

#define ATKBD_CMD_OK_GETID 0xe8
#define ATKBD_CMD_EX_ENABLE 0xea
#define ATKBD_CMD_EX_SETLEDS 0xeb
#define ATKBD_CMD_SETLEDS 0xed
#define ATKBD_CMD_DIAG_ECHO 0xee
#define ATKBD_CMD_GSCANSET 0xf0
#define ATKBD_CMD_SSCANSET 0xf0
#define ATKBD_CMD_GETID 0xf2
#define ATKBD_CMD_SETREP 0xf3
#define ATKBD_CMD_ENABLE 0xf4
#define ATKBD_CMD_RESET_DIS 0xf5
#define ATKBD_CMD_RESET_DEF 0xf6
#define ATKBD_CMD_ALL_TYPEM 0xf7
#define ATKBD_CMD_SETALL_MB 0xf8
#define ATKBD_CMD_SETALL_MBR 0xfa
#define ATKBD_CMD_SET_A_KEY_T 0xfb
#define ATKBD_CMD_SET_A_KEY_MR 0xfc
#define ATKBD_CMD_SET_A_KEY_M 0xfd
#define ATKBD_CMD_RESEND 0xfe
#define ATKBD_CMD_RESET 0xff

#endif /* __CROS_EC_ATKBD_PROTOCOL_H */
