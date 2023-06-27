/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "retimer/kb8010_public.h"
#include "usbc/usb_muxes.h"

const struct kb8010_control kb8010_controls[] = {
	USB_MUX_KB8010_CONTROLS_ARRAY
};
