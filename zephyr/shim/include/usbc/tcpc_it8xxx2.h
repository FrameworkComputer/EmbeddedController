/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/devicetree.h>
#include "driver/tcpm/it8xxx2_pd_public.h"

#define IT8XXX2_TCPC_COMPAT ite_it8xxx2_usbpd

#define TCPC_CONFIG_IT8XXX2(id)                   \
	{                                         \
		.bus_type = EC_BUS_TYPE_EMBEDDED, \
		.drv = &it8xxx2_tcpm_drv,         \
		.flags = 0,                       \
	},
