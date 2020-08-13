/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Validity tests for a state machine definition */

#ifndef __CROS_EC_USB_SM_CHECKS_H
#define __CROS_EC_USB_SM_CHECKS_H

int test_tc_no_parent_cycles(void);
int test_tc_all_states_named(void);


int test_prl_no_parent_cycles(void);
int test_prl_all_states_named(void);


int test_pe_no_parent_cycles(void);
int test_pe_all_states_named(void);

#endif /* __CROS_EC_USB_SM_CHECKS_H */