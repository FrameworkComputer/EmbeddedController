/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX5M5G SoC little FW used by booter
 */

#ifndef __CROS_EC_EC_LFW_H
#define __CROS_EC_EC_LFW_H

/* Begin address for the .iram section; defined in linker script */
extern unsigned int __iram_fw_start;
/* End address for the .iram section; defined in linker script */
extern unsigned int __iram_fw_end;
/* Begin address for the iram codes; defined in linker script */
extern unsigned int __flash_fw_start;

#endif /* __CROS_EC_EC_LFW_H */
