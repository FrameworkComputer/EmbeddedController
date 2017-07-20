/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* OTP memory module for Chrome EC */

#ifndef __CROS_EC_OTP_H
#define __CROS_EC_OTP_H

/*
 * OTP: One Time Programable memory is used for storing persistent data.
 */

/**
 * Set the serial number in OTP memory.
 *
 * @param serialno	ascii serial number string.
 *
 * @return success status.
 */
int otp_write_serial(const char *serialno);

/**
 * Get the serial number from flash.
 *
 * @return char * ascii serial number string.
 *     NULL if error.
 */
const char *otp_read_serial(void);

#endif  /* __CROS_EC_OTP_H */
