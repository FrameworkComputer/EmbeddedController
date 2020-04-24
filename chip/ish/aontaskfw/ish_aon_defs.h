/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ISH_AON_DEFS_H
#define __CROS_EC_ISH_AON_DEFS_H

#include "ia_structs.h"

/* aontask entry point function */
void ish_aon_main(void);

#ifdef CONFIG_ISH_IPAPG
extern int ipapg(void);
extern void pg_exit_restore_ctx(void);
extern void pg_exit_save_ctx(void);
#else
static int ipapg(void)
{
	return 0;
}
static void pg_exit_restore_ctx(void)
{
}
static void pg_exit_save_ctx(void)
{
}
#endif

struct gdt_header mainfw_gdt;
uint16_t tr;

#define FPU_REG_SET_SIZE 108
uint8_t fpu_reg_set[FPU_REG_SET_SIZE];

#endif /* __CROS_EC_ISH_AON_DEFS_H */
