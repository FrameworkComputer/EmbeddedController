/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief Private header file. Not meant to be used outside of mpu.c and tests.
 */

#ifndef __CROS_EC_MPU_PRIVATE_H
#define __CROS_EC_MPU_PRIVATE_H

int mpu_num_regions(void);
bool has_mpu(void);
bool mpu_is_unified(void);
void mpu_disable(void);
int mpu_update_region(uint8_t region, uint32_t addr, uint8_t size_bit,
		      uint16_t attr, uint8_t enable, uint8_t srd);
int mpu_config_region(uint8_t region, uint32_t addr, uint32_t size,
		      uint16_t attr, uint8_t enable);
struct mpu_rw_regions mpu_get_rw_regions(void);

#endif /* __CROS_EC_MPU_PRIVATE_H */
