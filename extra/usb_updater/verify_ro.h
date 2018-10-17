/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EXTRA_USB_UPDATER_VERIFY_RO_H
#define __EXTRA_USB_UPDATER_VERIFY_RO_H

#include <stdbool.h>

#include "gsctool.h"

/*
 * Runs RO verification on the target specified in td using the description file
 * desc_file_name. If show_machine_output is set, target's board ID will be
 * outputted in a machine-friendly format. Returns 0 on success or a negative
 * value if there is an error.
 */
int verify_ro(struct transfer_descriptor *td,
	      const char *desc_file_name,
	      bool show_machine_output);

#endif // __EXTRA_USB_UPDATER_VERIFY_RO_H
