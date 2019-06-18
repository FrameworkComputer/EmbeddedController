/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

SECTIONS
{
	.roshared : { KEEP(*(.roshared*)) }
	/*
	* Save the .ARM.atrributes section to make the linker not complain
	* about conflicting CPU architectures when linking with the RW objs.
	* This section will be discarded by the main EC linker script.
	*/
	.ARM.attributes : { KEEP(*(.ARM.*)) }
}
