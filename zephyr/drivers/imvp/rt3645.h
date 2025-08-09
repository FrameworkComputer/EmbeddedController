/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_IMVP_RT3645_H_
#define ZEPHYR_DRIVERS_IMVP_RT3645_H_

#include <zephyr/device.h>
#include <zephyr/types.h>

#define NVM_STAT_REG 0xEC
#define NVM_RELOAD_STAT_BIT 0x7
#define NVM_PRGRM_FINISH_STAT_BIT 0x6
#define NVM_STAT 0x0

#define NVM_PRGRM_CTRL_REG 0xED
#define NVM_PRGRM_DAT 0xAA
#define NVM_RESTORE_DAT 0x66

#define CONFIG_MODE_REG 0xF1

#define PRODUCT_ID_REG 0xFE

#define ICC_MAX_REG 0x00
#define ICC_MAX_RAILA_VAL 0x32
#define ICC_MAX_RAILC_VAL 0x32

#define ICCMAX_HC_SR_KTON_REG 0x01
#define ICCMAX_HC_SR_KTON_RAILA_VAL 0x27
#define ICCMAX_HC_SR_KTON_RAILB_VAL 0x2A
#define ICCMAX_HC_SR_KTON_RAILC_VAL 0x28
#define ICCMAX_HC_SR_KTON_RAILD_VAL 0x29

#define VBOOT_EN_RIMON_MSB_REG 0x02

#define RIMON_REG 0x03
#define RIMON_RAILA_VAL 0xAF
#define RIMON_RAILC_VAL 0xB6

#define RLL_REG 0x04
#define RLL_PAGE5_VAL 0xA5
#define RLL_RAILA_VAL 0xD0
#define RLL_RAILC_VAL 0x00

#define VID_STEP_COMP_GAIN_REG 0x05
#define VID_STEP_COMP_GAIN_RAILC_VAL 0x3E

#define COMP_MODE_PZ_REG 0x06
#define COMP_MODE_PZ_RAILC_VAL 0x3C

#define VSEN_COMP_LPF_REG 0x07
#define VSEN_COMP_LPF_RAILC_VAL 0x2B
#define VSEN_COMP_LPF_RAILD_VAL 0x30

#define DVID_ENHANCE_SPM_EN_REG 0x08
#define DVID_ENHANCE_SPM_EN_RAILA_VAL 0xAD
#define DVID_ENHANCE_SPM_EN_RAILB_VAL 0x89
#define DVID_ENHANCE_SPM_EN_RAILC_VAL 0x03
#define DVID_ENHANCE_SPM_EN_RAILD_VAL 0xED

#define DEM_SHRINK_TON_REG 0x0C
#define DEM_SHRINK_TON_PAGE5_VAL 0xC1
#define DEM_SHRINK_TON_RAILC_VAL 0x11

#define ZCD_VID_R_TH_REG 0x0D
#define ZCD_VID_R_TH_RAILC_VAL 0xF8

#define ZCD_I_TH_HYS_REG 0x0E
#define ZCD_I_TH_HYS_RAILC_VAL 0xBF

#define DVID_TAU_AQR_TH_REG 0x0F
#define DVID_TAU_AQR_TH_RAILC_VAL 0x45

#define RIPPLE_COMP_SVID_ADDR_REG 0x10
#define RIPPLE_COMP_SVID_ADDR_RAILC_VAL 0x01
#define RIPPLE_COMP_SVID_ADDR_RAILD_VAL 0x013

#define SVID_DCLL_REG 0x12
#define SVID_DCLL_RAILC_VAL 0x00

#define SPM_HYS_DVIDUP_PH_A_REG 0x05
#define SPM_HYS_DVIDUP_PH_A_REG_VAL 0x70

#define DVIDUP_PH_B_DVIDDN_PH_SET_REG 0x07
#define DVIDUP_PH_B_DVIDDN_PH_SET_REG_VAL 0x40

#define PAGE_SET_REG 0xEF
#define CRC_REG 0x13

int rt3645_read_reg(const struct device *dev, uint8_t reg, uint8_t *val);

int rt3645_write_reg(const struct device *dev, uint8_t reg, uint8_t val);

int rt3645_set_cfg_mode(const struct device *dev, const uint8_t *seq,
			uint8_t seq_len);

int rt3645_set_page(const struct device *dev, uint8_t page);

int rt3645_load_config(const struct device *dev);

int rt3645_store_config(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_IMVP_RT3645_H_ */
