/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Intel ADL-RVP specific configuration */

#ifndef __ADLRVP_BOARD_H
#define __ADLRVP_BOARD_H

#include "config.h"

#define I2C_ADDR_FUSB302_TCPC_AIC 0x22
#define I2C_ADDR_SN5S330_TCPC_AIC_PPC 0x40

#define I2C_ADDR_PCA9675_TCPC_AIC_IOEX 0x21

/* SOC side BB retimers (dual retimer config) */
#define I2C_PORT0_BB_RETIMER_SOC_ADDR 0x54
#if defined(HAS_TASK_PD_C1)
#define I2C_PORT1_BB_RETIMER_SOC_ADDR 0x55
#endif

#define ADLM_LP4_RVP1_SKU_BOARD_ID 0x01
#define ADLM_LP5_RVP2_SKU_BOARD_ID 0x02
#define ADLM_LP5_RVP3_SKU_BOARD_ID 0x03
#define ADLN_LP5_ERB_SKU_BOARD_ID 0x06
#define ADLN_LP5_RVP_SKU_BOARD_ID 0x07
#define ADLP_DDR5_RVP_SKU_BOARD_ID 0x12
#define ADLP_LP5_T4_RVP_SKU_BOARD_ID 0x13
#define ADL_RVP_BOARD_ID(id) ((id) & 0x3F)

enum adlrvp_charge_ports {
	TYPE_C_PORT_0,
#if defined(HAS_TASK_PD_C1)
	TYPE_C_PORT_1,
#endif
#if defined(HAS_TASK_PD_C2)
	TYPE_C_PORT_2,
#endif
#if defined(HAS_TASK_PD_C3)
	TYPE_C_PORT_3,
#endif
};

enum ioex_port {
	IOEX_C0_PCA9675,
	IOEX_C1_PCA9675,
#if defined(HAS_TASK_PD_C2)
	IOEX_C2_PCA9675,
	IOEX_C3_PCA9675,
#endif
	IOEX_PORT_COUNT
};

#endif /* __ADLRVP_BOARD_H */
