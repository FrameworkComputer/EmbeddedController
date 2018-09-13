/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "common.h"

/* Include USB PD Policy Engine State Machine */
#if defined(CONFIG_USB_TYPEC_VPD) || defined(CONFIG_USB_TYPEC_CTVPD)
#include "usb_pe_ctvpd_sm.h"
#else
#error "A USB PD Policy Engine State Machine must be defined."
#endif
