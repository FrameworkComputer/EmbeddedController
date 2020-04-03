/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * OCPC - One Charger IC per Type-C
 */

#ifndef __CROS_EC_OCPC_H_
#define __CROS_EC_OCPC_H_

#define PRIMARY_CHARGER   0
#define SECONDARY_CHARGER 1

struct ocpc_data {
	/* Index into chg_chips[] table for the charger IC that is switching. */
	int active_chg_chip;
};

#endif /* __CROS_EC_OCPC_H */
