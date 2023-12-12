/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MPU_H
#define __CROS_EC_MPU_H

/* This matches up with core/cortex-m/include/mpu.h */

/* Location of iram.text */
extern char __iram_text_start;
extern char __iram_text_end;

/** Enable MPU */
void mpu_enable(void);

/**
 * Returns the value of MPU type register
 *
 * @returns 0 for now (always)
 */
uint32_t mpu_get_type(void);

/** Protect RAM from code execution */
int mpu_protect_data_ram(void);

/** Protect code RAM from being overwritten */
int mpu_protect_code_ram(void);

/** Protect internal mapped flash memory from code execution */
int mpu_lock_ro_flash(void);
int mpu_lock_rw_flash(void);

#ifdef CONFIG_PLATFORM_EC_ROLLBACK_MPU_PROTECT
int mpu_lock_rollback(int lock);
#endif

#endif /* __CROS_EC_CPU_H */
