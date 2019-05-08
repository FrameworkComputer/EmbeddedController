/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/*
 * ISH Firmware status register contains currnet ISH FW status.
 * Communication protocol for Host(x64), CSME, and PMC uses this register.
 */

#ifndef __ISH_FWST_H
#define __ISH_FWST_H

#include "common.h"
#include "registers.h"

/*
 * IPC link is up(ready)
 * IPC can be used by other protocols
 */
#define IPC_ISH_FWSTS_ILUP_FIELD		0x01
#define IPC_ISH_FWSTS_ILUP_SHIFT		0
#define IPC_ISH_FWSTS_ILUP_MASK	\
	  (IPC_ISH_FWSTS_ILUP_FIELD << IPC_ISH_FWSTS_ILUP_SHIFT)

/*
 * HECI layer is up(ready)
 */
#define IPC_ISH_FWSTS_HUP_FIELD			0x01
#define IPC_ISH_FWSTS_HUP_SHIFT			1
#define IPC_ISH_FWSTS_HUP_MASK \
	  (IPC_ISH_FWSTS_HUP_FIELD << IPC_ISH_FWSTS_HUP_SHIFT)

/*
 * ISH FW reason reason
 */
#define IPC_ISH_FWSTS_FAIL_REASON_FIELD		0x0F
#define IPC_ISH_FWSTS_FAIL_REASON_SHIFT		2
#define IPC_ISH_FWSTS_FAIL_REASON_MASK \
	  (IPC_ISH_FWSTS_FAIL_REASON_FIELD << IPC_ISH_FWSTS_FAIL_REASON_SHIFT)

/*
 * ISH FW reset ID
 */
#define IPC_ISH_FWSTS_RESET_ID_FIELD		0x0F
#define IPC_ISH_FWSTS_RESET_ID_SHIFT		8
#define IPC_ISH_FWSTS_RESET_ID_MASK \
	  (IPC_ISH_FWSTS_RESET_ID_FIELD << IPC_ISH_FWSTS_RESET_ID_SHIFT)

/*
 * ISH FW status type
 */
enum {
	FWSTS_AFTER_RESET               = 0,
	FWSTS_WAIT_FOR_HOST             = 4,
	FWSTS_START_KERNEL_DMA          = 5,
	FWSTS_FW_IS_RUNNING             = 7,
	FWSTS_SENSOR_APP_LOADED         = 8,
	FWSTS_SENSOR_APP_RUNNING        = 15
};

/*
 * General ISH FW status
 */
#define IPC_ISH_FWSTS_FW_STATUS_FIELD		0x0F
#define IPC_ISH_FWSTS_FW_STATUS_SHIFT		12
#define IPC_ISH_FWSTS_FW_STATUS_MASK \
	  (IPC_ISH_FWSTS_FW_STATUS_FIELD << IPC_ISH_FWSTS_FW_STATUS_SHIFT)

#define IPC_ISH_FWSTS_DMA0_IN_USE_FIELD		0x01
#define IPC_ISH_FWSTS_DMA0_IN_USE_SHIFT		16
#define IPC_ISH_FWSTS_DMA0_IN_USE_MASK \
	  (IPC_ISH_FWSTS_DMA0_IN_USE_FIELD << IPC_ISH_FWSTS_DMA0_IN_USE_SHIFT)

#define IPC_ISH_FWSTS_DMA1_IN_USE_FIELD		0x01
#define IPC_ISH_FWSTS_DMA1_IN_USE_SHIFT		17
#define IPC_ISH_FWSTS_DMA1_IN_USE_MASK \
	  (IPC_ISH_FWSTS_DMA1_IN_USE_FIELD << IPC_ISH_FWSTS_DMA1_IN_USE_SHIFT)

#define IPC_ISH_FWSTS_DMA2_IN_USE_FIELD		0x01
#define IPC_ISH_FWSTS_DMA2_IN_USE_SHIFT		18
#define IPC_ISH_FWSTS_DMA2_IN_USE_MASK \
	  (IPC_ISH_FWSTS_DMA2_IN_USE_FIELD << IPC_ISH_FWSTS_DMA2_IN_USE_SHIFT)

#define IPC_ISH_FWSTS_DMA3_IN_USE_FIELD		0x01
#define IPC_ISH_FWSTS_DMA3_IN_USE_SHIFT		19
#define IPC_ISH_FWSTS_DMA3_IN_USE_MASK \
	  (IPC_ISH_FWSTS_DMA3_IN_USE_FIELD << IPC_ISH_FWSTS_DMA3_IN_USE_SHIFT)

#define IPC_ISH_FWSTS_POWER_STATE_FIELD		0x0F
#define IPC_ISH_FWSTS_POWER_STATE_SHIFT		20
#define IPC_ISH_FWSTS_POWER_STATE_MASK \
	  (IPC_ISH_FWSTS_POWER_STATE_FIELD << IPC_ISH_FWSTS_POWER_STATE_SHIFT)

#define IPC_ISH_FWSTS_AON_CHECK_FIELD		0x07
#define IPC_ISH_FWSTS_AON_CHECK_SHIFT		24
#define IPC_ISH_FWSTS_AON_CHECK_MASK \
	  (IPC_ISH_FWSTS_AON_CHECK_FIELD << IPC_ISH_FWSTS_AON_CHECK_SHIFT)

/* get ISH FW status register */
static inline uint32_t ish_fwst_get(void)
{
	return IPC_ISH_FWSTS;
}

/* set IPC link up */
static inline void ish_fwst_set_ilup(void)
{
	IPC_ISH_FWSTS |= (1<<IPC_ISH_FWSTS_ILUP_SHIFT);
}

/* clear IPC link up */
static inline void ish_fwst_clear_ilup(void)
{
	IPC_ISH_FWSTS &= ~IPC_ISH_FWSTS_ILUP_MASK;
}

/* return IPC link up state */
static inline int ish_fwst_is_ilup_set(void)
{
	return !!(IPC_ISH_FWSTS & IPC_ISH_FWSTS_ILUP_MASK);
}

/* set HECI up */
static inline void ish_fwst_set_hup(void)
{
	IPC_ISH_FWSTS |= (1<<IPC_ISH_FWSTS_HUP_SHIFT);
}

/* clear HECI up */
static inline void ish_fwst_clear_hup(void)
{
	IPC_ISH_FWSTS &= ~IPC_ISH_FWSTS_HUP_MASK;
}

/* get HECI up status */
static inline int ish_fwst_is_hup_set(void)
{
	return !!(IPC_ISH_FWSTS & IPC_ISH_FWSTS_HUP_MASK);
}

/* set fw failure reason */
static inline void ish_fwst_set_fail_reason(uint32_t val)
{
	uint32_t fwst = IPC_ISH_FWSTS;

	IPC_ISH_FWSTS = (fwst & ~IPC_ISH_FWSTS_FAIL_REASON_MASK) |
		(val << IPC_ISH_FWSTS_FAIL_REASON_SHIFT);
}

/* get fw failure reason */
static inline uint32_t ish_fwst_get_fail_reason(void)
{
	return (IPC_ISH_FWSTS & IPC_ISH_FWSTS_FAIL_REASON_MASK)
		>> IPC_ISH_FWSTS_FAIL_REASON_SHIFT;
}

/* set reset id */
static inline void ish_fwst_set_reset_id(uint32_t val)
{
	uint32_t fwst = IPC_ISH_FWSTS;

	IPC_ISH_FWSTS = (fwst & ~IPC_ISH_FWSTS_RESET_ID_MASK) |
		(val << IPC_ISH_FWSTS_RESET_ID_SHIFT);
}

/* get reset id */
static inline uint32_t ish_fwst_get_reset_id(void)
{
	return (IPC_ISH_FWSTS & IPC_ISH_FWSTS_RESET_ID_MASK)
		>> IPC_ISH_FWSTS_RESET_ID_SHIFT;
}

/* set general fw status */
static inline void ish_fwst_set_fw_status(uint32_t val)
{
	uint32_t fwst = IPC_ISH_FWSTS;

	IPC_ISH_FWSTS = (fwst & ~IPC_ISH_FWSTS_FW_STATUS_MASK) |
		(val << IPC_ISH_FWSTS_FW_STATUS_SHIFT);
}

/* get general fw status */
static inline uint32_t ish_fwst_get_fw_status(void)
{
	return (IPC_ISH_FWSTS & IPC_ISH_FWSTS_FW_STATUS_MASK)
		>> IPC_ISH_FWSTS_FW_STATUS_SHIFT;
}

#endif /* __ISH_FWST_H */
