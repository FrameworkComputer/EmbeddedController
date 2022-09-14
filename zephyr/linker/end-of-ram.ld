/* Copyright 2022 The Chromium OS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This section simply ensures there's enough unused space at the end of ram to
 * hold panic and jump data. This space isn't directly allocated because it
 * would result in the SRAM utilization always being reported as 100%.
 */
#if !defined(CONFIG_BOARD_NATIVE_POSIX)
SECTION_PROLOGUE(.end_of_ram_info, 0, )
{
	ASSERT(DEFINED(_image_ram_end) && _image_ram_end > 0, "Error: _image_ram_end is not defined");

#if defined(RAM_ADDR) && defined(RAM_SIZE)
	PROVIDE(__unused_ram_start = _image_ram_end);
	PROVIDE(__unused_ram_end = RAM_ADDR + RAM_SIZE);
	PROVIDE(__unused_ram_size = __unused_ram_end - __unused_ram_start);
	ASSERT(__unused_ram_size >= CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE,
	       "ERROR: Not enough space for preserved end of ram data (see PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE)");
#endif

#if defined(CONFIG_SHAREDMEM_MINIMUM_SIZE)
	ASSERT(CONFIG_SHAREDMEM_MINIMUM_SIZE >= CONFIG_PLATFORM_EC_PRESERVED_END_OF_RAM_SIZE,
	       "ERROR: Sharedmem must be at least large enough for preserved end of ram data");
#endif

}
#endif
