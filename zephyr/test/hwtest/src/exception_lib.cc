/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <exception>

void exception_lib_throw(void)
{
	throw std::exception();
}
