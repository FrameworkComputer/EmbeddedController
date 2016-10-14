/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_STDINT_H__
#define __CROS_EC_STDINT_H__

typedef unsigned char      uint8_t;
typedef signed char        int8_t;

typedef unsigned short     uint16_t;
typedef signed short       int16_t;

typedef unsigned int       uint32_t;
typedef signed int         int32_t;

typedef unsigned long long uint64_t;
typedef signed long long   int64_t;

typedef int                intptr_t;
typedef unsigned int       uintptr_t;

typedef uint8_t            uint_least8_t;

#ifndef UINT16_MAX
#define UINT16_MAX (65535U)
#endif
#ifndef INT16_MAX
#define INT16_MAX (32767U)
#endif

#ifndef UINT32_MAX
#define UINT32_MAX (4294967295U)
#endif
#ifndef INT32_MAX
#define INT32_MAX (2147483647U)
#endif

#endif /* __CROS_EC_STDINT_H__ */
