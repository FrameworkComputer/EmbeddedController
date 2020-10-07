/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _OCTOPUS_CBI_SSFC__H_
#define _OCTOPUS_CBI_SSFC__H_

/****************************************************************************
 * Octopus CBI Second Source Factory Cache
 */

/*
 * TCPC Port 1 (Bits 0-2)
 */
enum ssfc_tcpc_p1 {
	SSFC_TCPC_P1_DEFAULT,
	SSFC_TCPC_P1_PS8751,
	SSFC_TCPC_P1_PS8755,
};
#define SSFC_TCPC_P1_OFFSET		0
#define SSFC_TCPC_P1_MASK		GENMASK(2, 0)

/*
 * PPC Port 1 (Bits 3-5)
 */
enum ssfc_ppc_p1 {
	SSFC_PPC_P1_DEFAULT,
	SSFC_PPC_P1_NX20P348X,
	SSFC_PPC_P1_SYV682X,
};
#define SSFC_PPC_P1_OFFSET		3
#define SSFC_PPC_P1_MASK		GENMASK(5, 3)

enum ssfc_tcpc_p1 get_cbi_ssfc_tcpc_p1(void);
enum ssfc_ppc_p1 get_cbi_ssfc_ppc_p1(void);

#endif /* _OCTOPUS_CBI_SSFC__H_ */
