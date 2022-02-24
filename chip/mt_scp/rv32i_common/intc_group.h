/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_INTC_GROUP_H
#define __CROS_EC_INTC_GROUP_H

#include "common.h"
#include "intc.h"

/*
 * INTC_GRP_0 is reserved.  See swirq of syscall_handler() in
 * core/riscv-rv32i/task.c for more details.
 *
 * Lower group has higher priority.  Group 0 has highest priority.
 */
enum INTC_GROUP {
	INTC_GRP_0 = 0x0,
	INTC_GRP_1,
	INTC_GRP_2,
	INTC_GRP_3,
	INTC_GRP_4,
	INTC_GRP_5,
	INTC_GRP_6,
	INTC_GRP_7,
	INTC_GRP_8,
	INTC_GRP_9,
	INTC_GRP_10,
	INTC_GRP_11,
	INTC_GRP_12,
	INTC_GRP_13,
	INTC_GRP_14,
};

struct intc_irq_group {
	uint8_t group;
};

uint8_t intc_irq_group_get(int irq);

#endif /* __CROS_EC_INTC_GROUP_H */
