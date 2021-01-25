/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../../third_party/libaeabi-cortexm0/core/cortex-m/ldivmod.S"
#include "../../third_party/libaeabi-cortexm0/core/cortex-m/uldivmod.S"

exception_panic:
	mov r0, #3 @ K_ERR_KERNEL_OOPS
	b z_fatal_error
