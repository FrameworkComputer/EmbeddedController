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

#include <stdbool.h>

#define TEST_PORT 0

/* Arbitrary */
#define PARTNER_PRODUCT_ID 0x1234
#define PARTNER_DEV_BINARY_CODED_DECIMAL 0x5678

BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

struct usbc_alt_mode_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
};

struct usbc_alt_mode_custom_discovery_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
};

struct usbc_alt_mode_dp_unsupported_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
};

struct usbc_alt_mode_minus_dp_configure_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
};

struct usbc_discovery_no_drs_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
};

/** Assert that the TCPM sent or did not send a Data Reset message.
 *
 * Fail an assertion if the partner message log
 * 1) does not contain a Data Reset sent by the TCPM when one was expected, or
 * 2) does contain a Data Reset sent by the TCPM when none was expected.
 *
 * @param partner Partner emulator
 * @param want Whether the TCPM is expected to have sent a Data Reset
 */
void verify_data_reset_msg(struct tcpci_partner_data *partner, bool want);

/** Simulate a connection between the TCPM and the partner emulator.
 *
 * Take enough time to leave a normal connection in a settled state.
 *
 * @param tcpc_emul An emulator for the local TCPC
 * @param charger_emul An emulator for the local charger
 * @param partner_emul An emulator for the partner to be connected
 * @param src_ext An initialized source extension to be used by partner_emul
 */
void connect_partner_to_port(const struct emul *tcpc_emul,
			     const struct emul *charger_emul,
			     struct tcpci_partner_data *partner_emul,
			     const struct tcpci_src_emul_data *src_ext);

/** Simulate disconnecting the TCPM and the partner emulator.
 *
 * Take enough time to leave a normal connection in a settled state.
 *
 * @param tcpc_emul An emulator for the local TCPC
 * @param charger_emul An emulator for the local charger
 */
void disconnect_partner_from_port(const struct emul *tcpc_emul,
				  const struct emul *charger_emul);

#endif /* ZEPHYR_TEST_DRIVERS_USBC_ALT_MODE_TEST_USBC_ALT_MODE_H_ */
