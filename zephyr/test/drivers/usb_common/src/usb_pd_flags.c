/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"
#include "usb_pd_flags.h"

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

ZTEST_USER(usb_common, test_usb_pd_charger_otg)
{
	set_usb_pd_charger_otg(USB_PD_CHARGER_OTG_ENABLED);
	zassert_equal(get_usb_pd_charger_otg(), USB_PD_CHARGER_OTG_ENABLED);

	set_usb_pd_charger_otg(USB_PD_CHARGER_OTG_DISABLED);
	zassert_equal(get_usb_pd_charger_otg(), USB_PD_CHARGER_OTG_DISABLED);
}

ZTEST_USER(usb_common, test_usb_pd_vbus_detect)
{
	set_usb_pd_vbus_detect(USB_PD_VBUS_DETECT_TCPC);
	zassert_equal(get_usb_pd_vbus_detect(), USB_PD_VBUS_DETECT_TCPC);

	set_usb_pd_vbus_detect(USB_PD_VBUS_DETECT_CHARGER);
	zassert_equal(get_usb_pd_vbus_detect(), USB_PD_VBUS_DETECT_CHARGER);
}

ZTEST_USER(usb_common, test_usb_pd_discharge)
{
	set_usb_pd_discharge(USB_PD_DISCHARGE_PPC);
	zassert_equal(get_usb_pd_discharge(), USB_PD_DISCHARGE_PPC);

	set_usb_pd_discharge(USB_PD_DISCHARGE_GPIO);
	zassert_equal(get_usb_pd_discharge(), USB_PD_DISCHARGE_GPIO);
}
