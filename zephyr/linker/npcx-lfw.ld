/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

. = ALIGN(4);
__lfw_text_start = .;
*(.lfw.*)
. = ALIGN(4);
__lfw_text_end = .;
