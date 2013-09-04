/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* List of valid params for the generic get/set value operation */

#ifndef __CROS_EC_GETSET_H
#define __CROS_EC_GETSET_H

#include <stdint.h>
#include "ec_commands.h"

/* Define the params. */
#define GSV_ITEM(n, v) GSV_PARAM_##n,
#include "getset_value_list.h"
enum gsv_param_id {
	GSV_LIST
	NUM_GSV_PARAMS
};
#undef GSV_ITEM

/* Declare the storage where the values will be kept. */
extern uint32_t gsv[];

#endif /* __CROS_EC_GETSET_H */
