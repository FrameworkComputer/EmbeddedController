/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ANDROID_H__
#define ANDROID_H__

/*
 * in_android - Test to see if the underlying OS is Android
 *
 * returns boolean 1 to indicate true, 0 otherwise
 */
extern int in_android(void);

/**
 * Determine where temporary files go.
 *
 * @return A pointer to value of directory containing temporary files if
 *         successful, or NULL otherwise
 */
extern char *android_tmpdir_path(void);

#endif
