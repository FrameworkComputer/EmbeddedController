/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* FPU module for Chrome EC operating system */

#ifndef __EC_FPU_H
#define __EC_FPU_H

/* Enables FPU. Note that this function also disables interrupt. */
void enable_fpu(void);

/* Disables FPU. This function also enables interrupt. */
void disable_fpu(void);

#endif  /* __EC_FPU_H */
