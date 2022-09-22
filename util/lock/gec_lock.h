/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __UTIL_GEC_LOCK_H
#define __UTIL_GEC_LOCK_H

/*
 * acquire_gec_lock  -  acquire global lock
 *
 * returns 0 to indicate lock acquired
 * returns >0 to indicate lock was already held
 * returns <0 to indicate failed to acquire lock
 */
extern int acquire_gec_lock(int timeout_secs);

/*
 * release_gec_lock  -  release global lock
 *
 * returns 0 if lock was released successfully
 * returns -1 if lock had not been held before the call
 */
extern int release_gec_lock(void);

#endif /* __UTIL_GEC_LOCK_H */
