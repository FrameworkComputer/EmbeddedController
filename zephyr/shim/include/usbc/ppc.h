/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_CHROME_USBC_PPC_H
#define ZEPHYR_CHROME_USBC_PPC_H

#include "usbc/ppc_aoz1380.h"
#include "usbc/ppc_ktu1125.h"
#include "usbc/ppc_nx20p348x.h"
#include "usbc/ppc_rt1739.h"
#include "usbc/ppc_sn5s330.h"
#include "usbc/ppc_syv682x.h"
#include "usbc/utils.h"
#include "usbc_ppc.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief List of all supported PPC drivers and emulators. Format is
 * (compatible, config).
 */
/* clang-format off */
#define PPC_DRIVERS                              \
	(AOZ1380_COMPAT, PPC_CHIP_AOZ1380),      \
	(KTU1125_COMPAT, PPC_CHIP_KTU1125),      \
	(NX20P348X_COMPAT, PPC_CHIP_NX20P348X),  \
	(RT1739_PPC_COMPAT, PPC_CHIP_RT1739),    \
	(SN5S330_COMPAT, PPC_CHIP_SN5S330),      \
	(SN5S330_EMUL_COMPAT, PPC_CHIP_SN5S330), \
	(SYV682X_COMPAT, PPC_CHIP_SYV682X),      \
	(SYV682X_EMUL_COMPAT, PPC_CHIP_SYV682X)
/* clang-format on */

/**
 * @brief List of TCPC compatible strings only.
 */
#define PPC_DRIVER_COMPATS \
	FOR_EACH(USBC_DRIVER_GET_COMPAT_COMMA, (), PPC_DRIVERS)

/**
 * @brief Create a unique name based on a PPC altnernate node.
 *
 *	ppc_syv682x_alt: syv682x@43 {
 *		compatible = "silergy,syv682x";
 *		status = "okay";
 *		reg = <0x43>;
 *		frs_en_gpio = <&ioex_usb_c0_frs_en>;
 *		is-alt;
 *	};
 *
 * Usage:
 *	PPC_ALT_NAME_GET(DT_NODELABEL(ppc_syv682x_alt))
 *
 * expands to "ppc_alt_DT_N_S_i2c_100_S_syv682x_43"
 */
#define PPC_ALT_NAME_GET(node_id) DT_CAT(ppc_alt_, node_id)

/**
 * @brief Get the PPC alternate entry based on a nodelabel.
 *
 * Usage:
 *	PPC_ALT_FROM_NODELABEL(ppc_syv682x_alt))
 *
 * expands to "ppc_alt_DT_N_S_i2c_100_S_syv682x_43"
 */
#define PPC_ALT_FROM_NODELABEL(lbl) (PPC_ALT_NAME_GET(DT_NODELABEL(lbl)))

/**
 * @brief - Forward declare a global struct ppc_config_t entry based on
 * a single PPC altnerate from the devicetree.
 */
#define PPC_ALT_DECLARATION(node_id) \
	extern const struct ppc_config_t PPC_ALT_NAME_GET(node_id)

#define PPC_ALT_DECLARE(node_id, config_fn)         \
	COND_CODE_1(DT_PROP_OR(node_id, is_alt, 0), \
		    (PPC_ALT_DECLARATION(node_id);), ())

/*
 * Forward declare a struct ppc_config_t for every PPC node in the tree with the
 * "is-alt" property set.
 */
DT_FOREACH_USBC_DRIVER_STATUS_OK_VARGS(PPC_ALT_DECLARE, PPC_DRIVERS)

extern struct ppc_config_t ppc_chips_alt[];

#define ALT_PPC_CHIP_CHK(usbc_id, usb_port_num)                              \
	COND_CODE_1(DT_REG_HAS_IDX(usbc_id, usb_port_num),                   \
		    (COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, ppc_alt), (|| 1), \
				 (|| 0))),                                   \
		    (|| 0))

#define PPC_ENABLE_ALTERNATE(usb_port_num)                                            \
	do {                                                                          \
		BUILD_ASSERT(                                                         \
			(0 DT_FOREACH_STATUS_OKAY_VARGS(named_usbc_port,              \
							ALT_PPC_CHIP_CHK,             \
							usb_port_num)),               \
			"Selected USB node does not exist or does not specify a PPC " \
			"alternate chip");                                            \
		memcpy(&ppc_chips[usb_port_num], &ppc_chips_alt[usb_port_num],        \
		       sizeof(struct ppc_config_t));                                  \
	} while (0)

#define PPC_ENABLE_ALTERNATE_BY_NODELABEL(usb_port_num, nodelabel)           \
	memcpy(&ppc_chips[usb_port_num], &PPC_ALT_FROM_NODELABEL(nodelabel), \
	       sizeof(struct ppc_config_t))

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_CHROME_USBC_PPC_H */
