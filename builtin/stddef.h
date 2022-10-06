/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_STDDEF_H__
#define __CROS_EC_STDDEF_H__

#ifndef __SIZE_TYPE__
#define __SIZE_TYPE__ unsigned long
#endif

typedef __SIZE_TYPE__ size_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef __WCHAR_TYPE__
#define __WCHAR_TYPE__ int
#endif

#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif

/* This macro definition is duplicated in compile_time_macros.h. It still needs
 * to be defined here to support code that expects offsetof to be defined in the
 * standard location (this file). Both definitions are guarded by a #ifndef
 * check for safety.
 */
#ifndef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#endif

#endif /* __CROS_EC_STDDEF_H__ */
