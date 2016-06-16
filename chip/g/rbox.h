/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_RBOX_H
#define __CROS_RBOX_H

#include "console.h"
#include "registers.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_RBOX, outstr)
#define CPRINTS(format, args...) cprints(CC_RBOX, format, ## args)

#ifdef CONFIG_RBOX_DEBUG
#define INTR(field) CONCAT2(INTR_, field)

#define ENABLE_INT(field) GWRITE_FIELD(RBOX, INT_ENABLE, INTR(field), 1)
#define ENABLE_INT_RF(field) (ENABLE_INT(CONCAT2(field, _RED)) &&	\
	ENABLE_INT(CONCAT2(field, _FED)))

#define RBOX_INT(NAME, NAME_STR)					\
									\
	DECLARE_IRQ(CONCAT3(GC_IRQNUM_RBOX0_INTR_, NAME, _INT),		\
		CONCAT2(NAME, _int_), 1);				\
	void CONCAT2(NAME, _int_)(void)					\
	{								\
		CPRINTS("%s", NAME_STR);				\
		/* Clear interrupt */					\
		GWRITE_FIELD(RBOX, INT_STATE, INTR(NAME), 1);		\
	}

#endif  /* DEBUG_RBOX */
#endif  /* __CROS_RBOX_H */
