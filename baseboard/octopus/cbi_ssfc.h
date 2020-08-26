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
	TCPC_P1_DEFAULT,
	TCPC_P1_PS8751,
	TCPC_P1_PS8755,
};
#define SSFC_TCPC_P1_OFFSET		0
#define SSFC_TCPC_P1_MASK		GENMASK(2, 0)

enum ssfc_tcpc_p1 get_cbi_ssfc_tcpc_p1(void);

#endif /* _OCTOPUS_CBI_SSFC__H_ */
