/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_ISH_AON_SHARE_H
#define __CROS_EC_ISH_AON_SHARE_H

#include "common.h"
#include "ia_structs.h"
#include "power_mgt.h"

/* magic ID for valid aontask image check */
#define AON_MAGIC_ID			        0x544E4F41  /*"AONT"*/

/* aontask error code  */
#define AON_SUCCESS				0
#define AON_ERROR_NOT_SUPPORT_POWER_MODE	1
#define AON_ERROR_DMA_FAILED			2


/* shared data structure between main FW and aontask */
struct ish_aon_share {
	/* magic ID */
	uint32_t magic_id;
	/* error counter */
	uint32_t error_count;
	/* last error */
	int last_error;
	/* successfully exit from IPAPG or not */
	uint32_t pg_exit;
	/* high 32bits of 64 bits dram address for dma */
	uint32_t uma_msb;
	/* aontask's TSS segment entry */
	struct tss_entry *aon_tss;
	/* aontask's LDT start address */
	ldt_entry *aon_ldt;
	/* aontask's LDT's limit size */
	uint32_t aon_ldt_size;
	/* current power state, see chip/ish/power_mgt.h */
	enum ish_pm_state pm_state;
	/* for store/restore main FW's IDT */
	struct idt_header main_fw_idt_hdr;

	/**
	 * main FW's read only code and data region in main SRAM,
	 * address need 64 bytes align due to DMA requirement
	 */
	uint32_t main_fw_ro_addr;
	uint32_t main_fw_ro_size;

	/**
	 * main FW's read and write data region in main SRAM,
	 * address need 64 bytes align due to DMA requirement
	 */
	uint32_t main_fw_rw_addr;
	uint32_t main_fw_rw_size;
} __packed;

#endif /* __CROS_EC_ISH_AON_SHARE_H */
