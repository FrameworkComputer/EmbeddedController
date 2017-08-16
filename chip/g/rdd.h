/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_RDD_H
#define __CROS_RDD_H

/* Detach from debug cable */
void rdd_detached(void);

/* Attach to debug cable */
void rdd_attached(void);

#endif  /* __CROS_RDD_H */
