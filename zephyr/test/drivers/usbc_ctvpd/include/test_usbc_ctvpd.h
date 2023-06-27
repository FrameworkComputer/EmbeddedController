/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_USBC_ALT_MODE_TEST_USBC_ALT_MODE_H_
#define ZEPHYR_TEST_DRIVERS_USBC_ALT_MODE_TEST_USBC_ALT_MODE_H_

#include "compile_time_macros.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "emul/tcpc/emul_tcpci_partner_vpd.h"
#include "test/drivers/stubs.h"

struct common_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_vpd_emul_data vpd_ext;
};

struct usbc_ctvpd_fixture {
	struct common_fixture common;
};

#endif /* ZEPHYR_TEST_DRIVERS_USBC_ALT_MODE_TEST_USBC_ALT_MODE_H_ */
