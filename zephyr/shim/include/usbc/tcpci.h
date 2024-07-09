/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/tcpm/tcpci.h"
#include "usbc/tcpc_anx7447.h"
#include "usbc/tcpc_anx7447_emul.h"
#include "usbc/tcpc_ccgxxf.h"
#include "usbc/tcpc_fusb302.h"
#include "usbc/tcpc_generic_emul.h"
#include "usbc/tcpc_nct38xx.h"
#include "usbc/tcpc_ps8xxx.h"
#include "usbc/tcpc_ps8xxx_emul.h"
#include "usbc/tcpc_raa489000.h"
#include "usbc/tcpc_rt1715.h"
#include "usbc/tcpc_rt1718s.h"
#include "usbc/utils.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TCPCI_COMPAT cros_ec_tcpci

/**
 * @brief List of all supported TCPC drivers and emulators. Format is
 * (compatible, config).
 */
/* clang-format off */
#define TCPC_DRIVERS                                     \
	(ANX7447_TCPC_COMPAT, TCPC_CONFIG_ANX7447),      \
	(CCGXXF_TCPC_COMPAT, TCPC_CONFIG_CCGXXF),        \
	(FUSB302_TCPC_COMPAT, TCPC_CONFIG_FUSB302),      \
	(IT8XXX2_TCPC_COMPAT, TCPC_CONFIG_IT8XXX2),      \
	(PS8XXX_COMPAT, TCPC_CONFIG_PS8XXX),             \
	(NCT38XX_TCPC_COMPAT, TCPC_CONFIG_NCT38XX),      \
	(RAA489000_TCPC_COMPAT, TCPC_CONFIG_RAA489000),  \
	(RT1718S_TCPC_COMPAT, TCPC_CONFIG_RT1718S),      \
	(RT1715_TCPC_COMPAT, TCPC_CONFIG_RT1715),        \
	(TCPCI_COMPAT, TCPC_CONFIG_TCPCI),               \
	(TCPCI_EMUL_COMPAT, TCPC_CONFIG_TCPCI_EMUL),     \
	(PS8XXX_EMUL_COMPAT, TCPC_CONFIG_PS8XXX_EMUL),   \
	(ANX7447_EMUL_COMPAT, TCPC_CONFIG_ANX7447_EMUL), \
	(RT1718S_EMUL_COMPAT, TCPC_CONFIG_RT1718S_EMUL)
/* clang-format on */

/**
 * @brief List of TCPC compatible strings only.
 */
#define TCPC_DRIVER_COMPATS \
	FOR_EACH(USBC_DRIVER_GET_COMPAT_COMMA, (), TCPC_DRIVERS)

/* clang-format off */
#define TCPC_CONFIG_TCPCI(id)                            \
	{                                                \
		.bus_type = EC_BUS_TYPE_I2C,           \
		.i2c_info = {                          \
			.port = I2C_PORT_BY_DEV(id),   \
			.addr_flags = DT_REG_ADDR(id), \
		},                                     \
		.drv = &tcpci_tcpm_drv, \
	}
/* clang-format on */

/**
 * @brief Create a unique name based on a TCPC alternate node.
 *
 *	tcpc_rt1715_alt: rt1715@43 {
 *		compatible = "richtek,rt1715-tcpc";
 *		status = "okay";
 *		reg = <0x43>;
 *		is-alt;
 *	};
 *
 * Usage:
 *	TCPC_ALT_NAME_GET(DT_NODELABEL(tcpc_rt1715_alt))
 *
 * expands to "tcpc_alt_DT_N_S_i2c_100_S_rt1715_43"
 */
#define TCPC_ALT_NAME_GET(node_id) DT_CAT(tcpc_alt_, node_id)

/**
 * @brief Get the TCPC alternate entry based on a nodelabel.
 *
 * Usage:
 *	TCPC_ALT_FROM_NODELABEL(tcpc_rt1715_alt)
 *
 * expands to "tcpc_alt_DT_N_S_i2c_100_S_rt1715_43"
 */
#define TCPC_ALT_FROM_NODELABEL(lbl) (TCPC_ALT_NAME_GET(DT_NODELABEL(lbl)))

/**
 * @brief - Forward declare a global struct tcpc_config_t entry based on
 * a single TCPC alternate from the devicetree.
 */
#define TCPC_ALT_DECLARATION(node_id) \
	extern const struct tcpc_config_t TCPC_ALT_NAME_GET(node_id)

/**
 * @brief Forward declare a global "struct tcpc_config_t" entry, only if
 * the TCPC node contains the "is-alt" property.
 *
 * @param node_id Node ID of the TCPC device
 * @param config_fn Unused by this macro, but required by the
 *                  DT_FOREACH_USBC_DRIVER_STATUS_OK_VARGS() wrapper.
 */
#define TCPC_ALT_DECLARE(node_id, config_fn)        \
	COND_CODE_1(DT_PROP_OR(node_id, is_alt, 0), \
		    (TCPC_ALT_DECLARATION(node_id);), ())

/*
 * For all TCPC drivers/emulators, forward declare a struct
 * tcpc_config_t for every node in the tree with the "is-alt" property
 * set.
 */
DT_FOREACH_USBC_DRIVER_STATUS_OK_VARGS(TCPC_ALT_DECLARE, TCPC_DRIVERS)

#define TCPC_ENABLE_ALTERNATE_BY_NODELABEL(usb_port_num, nodelabel) \
	memcpy(&tcpc_config[usb_port_num],                          \
	       &TCPC_ALT_FROM_NODELABEL(nodelabel),                 \
	       sizeof(struct tcpc_config_t))

#ifdef __cplusplus
}
#endif
