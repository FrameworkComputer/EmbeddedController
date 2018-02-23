/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EXTRA_USB_UPDATER_VERIFY_RO_H
#define __EXTRA_USB_UPDATER_VERIFY_RO_H

#include "gsctool.h"

int verify_ro(struct transfer_descriptor *td,
	      const char *desc_file_name);

#endif // __EXTRA_USB_UPDATER_VERIFY_RO_H
