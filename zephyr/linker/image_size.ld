/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if defined(CONFIG_PLATFORM_EC_RW_FWID_OVERLAP)

/*
 * Somehow, the linker scripts of different architectures use different symbols
 * to indicate the image size.
 */
#if defined(CONFIG_CPU_CORTEX_M) || defined(CONFIG_CPU_CORTEX_R) || \
	defined(CONFIG_CPU_CORTEX_A)
/* Defined in include/zephyr/arch/arm/cortex_m/scripts/linker.ld */
__image_size = _flash_used;
#elif defined(CONFIG_RISCV)
/* Defined in include/zephyr/arch/riscv/common/linker.ld */
__image_size = __rom_region_size;
#endif /* CONFIG_CPU_CORTEX_M || CONFIG_CPU_CORTEX_R || CONFIG_CPU_CORTEX_A */

#elif defined(CONFIG_PLATFORM_EC_RW_TOTAL_IMAGE_SIZE)
__image_size = CONFIG_PLATFORM_EC_RW_TOTAL_IMAGE_SIZE;
#else
/*
 * Intentionally set to 0. Some components, such as EFS2, need this value.
 * Having it be 0 will make it easier to find and add new cores.
 */
__image_size = 0;
#endif
