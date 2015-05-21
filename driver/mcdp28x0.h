/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Megachips DisplayPort to HDMI protocol converter / level shifter driver.
 */

#ifndef MCDP28X0_H
#define MCDP28X0_H

#define MCDP_OUTBUF_MAX 16
#define MCDP_INBUF_MAX 16

#define MCDP_CMD_GETINFO       0x40
#define MCDP_CMD_GETDEVID      0x30
#define MCDP_CMD_APPSTEST      0x12
#define MCDP_CMD_APPSTESTPARAM 0x11
#define MCDP_CMD_ACK           0x0c

/* packet header (2 bytes: length + cmd) + data + footer (1byte: checksum) */
#define MCDP_RSP_LEN(len) (len + 3)
#define MCDP_LEN_GETINFO 12


/**
 * Enable mcdp driver.
 */
void mcdp_enable(void);

/**
 * Disable mcdp driver.
 */
void mcdp_disable(void);

/**
 * get get information command from mcdp.
 *
 * @info pointer to mcdp_info structure
 * @return zero if success, error code otherwise.
 */
int mcdp_get_info(struct mcdp_info  *info);

#endif
