/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __EC_INCLUDE_OTP_KEY_H
#define __EC_INCLUDE_OTP_KEY_H

#include <stddef.h>
#include <stdint.h>

#include <common.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTP_KEY_SIZE_BYTES 32
#define OTP_KEY_ADDR 0x300

/**
 * Initialize One-Time Programmable (OTP) Memory for Key Storage
 *
 * Not supported by all platforms.
 **/
void otp_key_init(void);

/**
 * Read key from OTP
 *
 * Not supported by all platforms.
 **/
enum ec_error_list otp_key_read(uint8_t *key_buffer);

/**
 * Provision Key in OTP, check if blank (unprovisioned) and write if so
 *
 * Not supported by all platforms.
 **/
enum ec_error_list otp_key_provision(void);

/**
 * Shutdown OTP
 *
 * The opposite operation of otp_init(), disable the hardware resources
 * used by OTP memory to save power.
 *
 * Not supported by all platforms.
 **/
void otp_key_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* __EC_INCLUDE_OTP_KEY_H */
