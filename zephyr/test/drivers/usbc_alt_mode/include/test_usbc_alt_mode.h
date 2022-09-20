/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_USBC_ALT_MODE_TEST_USBC_ALT_MODE_H_
#define ZEPHYR_TEST_DRIVERS_USBC_ALT_MODE_TEST_USBC_ALT_MODE_H_

#include "compile_time_macros.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "test/drivers/stubs.h"

#define TEST_PORT 0

/* Arbitrary */
#define PARTNER_PRODUCT_ID 0x1234
#define PARTNER_DEV_BINARY_CODED_DECIMAL 0x5678

BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

struct usbc_alt_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

struct usbc_alt_mode_dp_unsupported_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

#endif /* ZEPHYR_TEST_DRIVERS_USBC_ALT_MODE_TEST_USBC_ALT_MODE_H_ */
