/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Persistent EC options stored in EEPROM */

#ifndef __CROS_EC_EOPTION_H
#define __CROS_EC_EOPTION_H

#include "common.h"

/* Boolean options */
enum eoption_bool {
	EOPTION_BOOL_TEST = 0,  /* Test option */
};

/**
 * Initialize the module.
 */
void eoption_init(void);

/**
 * Return the current value of a boolean option.
 *
 * @param opt		Option to return
 * @return 0 if option is false, 1 if true.
 */
int eoption_get_bool(enum eoption_bool opt);

/**
 * Set the value of a boolean option
 *
 * @param opt		Option to set
 * @param value		New value for option
 *
 * @return EC_SUCCESS, or non-zero if error.
 */
int eoption_set_bool(enum eoption_bool opt, int value);

#endif  /* __CROS_EC_EOPTION_H */
