/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NPCX-specific SIB module for Chrome EC */

#ifndef __CROS_EC_NPCX_LPC_H
#define __CROS_EC_NPCX_LPC_H

void system_lpc_host_register_init(void);

/* End address for the .lpram section; defined in linker script */
extern unsigned int __lpram_fw_end;
/* Begin flash address for the lpram codes; defined in linker script */
extern unsigned int __flash_lpfw_start;
/* End flash address for the lpram codes; defined in linker script */
extern unsigned int __flash_lpfw_end;

#endif /* __CROS_EC_NPCX_LPC_H */
