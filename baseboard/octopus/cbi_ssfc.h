/* Copyright 2020 The ChromiumOS Authors
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
#define SSFC_TCPC_P1_OFFSET 0
#define SSFC_TCPC_P1_MASK GENMASK(2, 0)

/*
 * PPC Port 1 (Bits 3-5)
 */
enum ssfc_ppc_p1 {
	SSFC_PPC_P1_DEFAULT,
	SSFC_PPC_P1_NX20P348X,
	SSFC_PPC_P1_SYV682X,
};
#define SSFC_PPC_P1_OFFSET 3
#define SSFC_PPC_P1_MASK GENMASK(5, 3)

/*
 * Charger (Bits 8-6)
 */
enum ssfc_charger {
	SSFC_CHARGER_DEFAULT,
	SSFC_CHARGER_ISL9238,
	SSFC_CHARGER_BQ25710,
};
#define SSFC_CHARGER_OFFSET 6
#define SSFC_CHARGER_MASK GENMASK(8, 6)

/*
 * Audio (Bits 11-9)
 */

/*
 * Sensor (Bits 14-12)
 */
enum ssfc_sensor {
	SSFC_SENSOR_DEFAULT,
	SSFC_SENSOR_BMI160,
	SSFC_SENSOR_ICM426XX,
	SSFC_SENSOR_BMI260,
};
#define SSFC_SENSOR_OFFSET 12
#define SSFC_SENSOR_MASK GENMASK(14, 12)

enum ssfc_tcpc_p1 get_cbi_ssfc_tcpc_p1(void);
enum ssfc_ppc_p1 get_cbi_ssfc_ppc_p1(void);
enum ssfc_charger get_cbi_ssfc_charger(void);
enum ssfc_sensor get_cbi_ssfc_sensor(void);

#endif /* _OCTOPUS_CBI_SSFC__H_ */
