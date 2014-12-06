/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * NPCX5M5G SoC little FW used by booter
 */

#ifndef __CROS_EC_LFW_H_
#define __CROS_EC_LFW_H_

/* Begin address for the .iram section; defined in linker script */
extern unsigned int __iram_fw_start;
/* End address for the .iram section; defined in linker script */
extern unsigned int __iram_fw_end;
/* Begin address for the iram codes; defined in linker script */
extern unsigned int __flash_fw_start;

#endif /* __CROS_EC_LFW_H_ */
