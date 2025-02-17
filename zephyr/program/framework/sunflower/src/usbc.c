/* Copyright 2025 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cypress_pd_common.h"

__override int board_perform_error_recovery_port(int port)
{
	int pd_port;

	if (port == PD_PORT_1 || port == PD_PORT_3)
		pd_port = port - 1;
	else if (port == PD_PORT_0 || port == PD_PORT_2)
		pd_port = port + 1;
	else
		pd_port = port;

	return pd_port;
}
