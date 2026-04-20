/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _ITE_UNPADDED_LD_
#define _ITE_UNPADDED_LD_

#include <zephyr/linker/linker-tool.h>

.rom_unpadded (NOLOAD) :
{
	/* Calculate the padding needed for rom mpu alignment */
	__rom_mpu_padding = ABSOLUTE(__rom_region_end) - ABSOLUTE(__rodata_region_end);

	ASSERT(__rom_mpu_padding >= 0, "rom_mpu_padding is negative");
	ASSERT(__rom_mpu_padding <  __rom_region_size, "rom_mpu_padding is larger than ROM region");

	/* TODO: Calculate the padding needed for the start of the ILM flash region */

	/* Calculate the padding needed for end of ILM flash alignment */
	__ilm_flash_end_padding = ABSOLUTE(__text_region_end) - ABSOLUTE(__ilm_flash_end);

	ASSERT(__ilm_flash_end_padding >= 0, "ilm_flash_end_padding is negative");
	ASSERT(__ilm_flash_end_padding <  __rom_region_size, "ilm_flash_end_padding is larger than ROM region");

	/* Calculate the unpadded size of the ROM region */
	. = __rom_region_size - __rom_mpu_padding - __ilm_flash_end_padding;
} > ROM_UNPADDED

.ram_unpadded (NOLOAD) :
{
	/* Calculate the padding needed for h2ram_pool alignment */
#ifdef CONFIG_ESPI_IT8XXX2
	__h2ram_pool_padding = ABSOLUTE(_h2ram_pool_start) - ABSOLUTE(__data_region_end);
#else
	__h2ram_pool_padding = 0;
#endif

	ASSERT(__h2ram_pool_padding >= 0, "h2ram_pool_padding is negative");
	ASSERT(__h2ram_pool_padding <  __kernel_ram_size, "h2ram_pool_padding is larger than RAM region");

	__ilm_ram_size = ABSOLUTE(__ilm_ram_end) - ABSOLUTE(__ilm_ram_start);

	. = _image_ram_size + __ilm_ram_size - __h2ram_pool_padding;
} > RAM_UNPADDED

#endif /* _ITE_UNPADDED_LD_ */