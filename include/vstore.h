/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_VSTORE_H
#define __CROS_EC_VSTORE_H

#ifdef TEST_BUILD

/* Clear all vstore locks */
void vstore_clear_lock(void);

#endif /* TEST_BUILD */

#endif /* __CROS_EC_VSTORE_H */
