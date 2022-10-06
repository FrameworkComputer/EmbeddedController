/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "fpsensor_detect.h"

enum fp_transport_type get_fp_transport_type(void)
{
	return FP_TRANSPORT_TYPE_SPI;
}
