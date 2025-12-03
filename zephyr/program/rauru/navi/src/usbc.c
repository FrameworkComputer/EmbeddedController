/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "driver/tcpm/it83xx_pd.h"
#include "usb_pd.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

#ifdef CONFIG_USB_PD_TCPM_ITE_ON_CHIP
const struct cc_para_t *board_get_cc_tuning_parameter(enum usbpd_port port)
{
	const static struct cc_para_t
		cc_parameter[CONFIG_USB_PD_ITE_ACTIVE_PORT_COUNT] = {
			{
				/*
				 * tune the RC filter to avoid undershooting the
				 * eye limits. Based on empirical observation in
				 * b/460336574
				 */
				.rc_filter = IT83XX_TX_RC_FILTER_0_UNIT,
				.rising_time = IT83XX_TX_PRE_DRIVING_TIME_TRIM,
				.falling_time = IT83XX_TX_PRE_DRIVING_TIME_TRIM,
			},
			{
				.rc_filter = IT83XX_TX_RC_FILTER_TRIM,
				.rising_time = IT83XX_TX_PRE_DRIVING_TIME_TRIM,
				.falling_time = IT83XX_TX_PRE_DRIVING_TIME_TRIM,
			},
		};
	return &cc_parameter[port];
}
#endif
