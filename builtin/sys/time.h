/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SYS_TIME_H__
#define __CROS_EC_SYS_TIME_H__

#include <sys/types.h>

/**
 * Partial implementation of <sys/time.h> header:
 * https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/sys_time.h.html
 */

typedef int64_t time_t;
typedef int32_t suseconds_t;

struct timeval {
	time_t tv_sec; /* seconds */
	suseconds_t tv_usec; /* microseconds */
};

#endif /* __CROS_EC_SYS_TIME_H__ */
