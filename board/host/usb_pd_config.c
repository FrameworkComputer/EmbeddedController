/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery board configuration */

#include "test_util.h"

test_mockable void pd_select_polarity(int port, int polarity)
{
	/* Not implemented */
}

test_mockable void pd_tx_init(void)
{
	/* Not implemented */
}

test_mockable void pd_set_host_mode(int port, int enable)
{
	/* Not implemented */
}

test_mockable int pd_adc_read(int port, int cc)
{
	/* Not implemented */
	return 0;
}

test_mockable int pd_snk_is_vbus_provided(int port)
{
	/* Not implemented */
	return 1;
}
