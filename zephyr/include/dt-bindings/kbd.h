/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef DT_BINDINGS_KBD_H_
#define DT_BINDINGS_KBD_H_

#define KBD_RC(row, col) ((((row) & 0xff) << 8) | ((col) & 0xff))

#define KBD_RC_ROW(rc) ((rc >> 8) & 0xff)
#define KBD_RC_COL(rc) (rc & 0xff)

#define KBD_SCANCODE_F1 0x0005
#define KBD_SCANCODE_F2 0x0006
#define KBD_SCANCODE_F3 0x0004
#define KBD_SCANCODE_F4 0x000c
#define KBD_SCANCODE_F5 0x0003
#define KBD_SCANCODE_F6 0x000b
#define KBD_SCANCODE_F7 0x0083
#define KBD_SCANCODE_F8 0x000a
#define KBD_SCANCODE_F9 0x0001
#define KBD_SCANCODE_F10 0x0009
#define KBD_SCANCODE_F11 0x0078
#define KBD_SCANCODE_F12 0x0007
#define KBD_SCANCODE_F13 0x000f
#define KBD_SCANCODE_F14 0x0017
#define KBD_SCANCODE_F15 0x001f

#define KBD_SCANCODE_CAPSLOCK 0x0058
#define KBD_SCANCODE_DELETE 0xe071
#define KBD_SCANCODE_PAGEUP 0xe07d
#define KBD_SCANCODE_PAGEDOWN 0xe07a
#define KBD_SCANCODE_HOME 0xe06c
#define KBD_SCANCODE_END 0xe069

#endif /* DT_BINDINGS_KBD_H_ */
