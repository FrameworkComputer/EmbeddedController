/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DT_BINDINGS_KBD_H_
#define DT_BINDINGS_KBD_H_

#define KBD_RC(row, col) ((((row) & 0xff) << 8) | ((col) & 0xff))

#define KBD_RC_ROW(rc) ((rc >> 8) & 0xff)
#define KBD_RC_COL(rc) (rc & 0xff)

#endif /* DT_BINDINGS_KBD_H_ */
