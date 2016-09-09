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

/*
 * USB is only used for CCD, so only enable UTMI wakeups when RDD detects that
 * a debug accessory is attached and disable it as a wakeup source when the
 * cable is detached.
 */
int is_utmi_wakeup_allowed(void);
#endif  /* __CROS_RDD_H */
