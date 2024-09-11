/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 *
 * @brief PDC PDO helper functions
 */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc_pdo.h"
#include "include/usb_pd.h"

#include <zephyr/logging/log.h>

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
LOG_MODULE_REGISTER(emul_pdc_pdo);

#define EMUL_PDO_MAX_EPR_PDO_OFFSET 4

#define EMUL_PDO_FIXED_PDO_COMMON_FLAGS                                       \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_UNCONSTRAINED | PDO_FIXED_COMM_CAP | \
	 PDO_FIXED_DATA_SWAP)

#define EMUL_PDO_FIXED_SRC_FLAGS                               \
	(EMUL_PDO_FIXED_PDO_COMMON_FLAGS | PDO_FIXED_SUSPEND | \
	 PDO_FIXED_PEAK_CURR(PDO_PEAK_OVERCURR_110))
#define EMUL_PDO_FIXED_SNK_FLAGS (EMUL_PDO_FIXED_PDO_COMMON_FLAGS)

#define EMUL_PDO_FIXED1_SRC PDO_FIXED(12000, 5000, EMUL_PDO_FIXED_SRC_FLAGS)
#define EMUL_PDO_FIXED2_SRC PDO_FIXED(20000, 3000, EMUL_PDO_FIXED_SRC_FLAGS)

#define EMUL_PDO_FIXED_SNK PDO_FIXED(5000, 3000, EMUL_PDO_FIXED_SNK_FLAGS)
#define EMUL_PDO_BATT_SNK PDO_BATT(5000, 20000, 45000)
#define EMUL_PDO_VAR_SNK PDO_VAR(5000, 20000, 3000)

static uint32_t *get_pdo_data(struct emul_pdc_pdo_t *pdos,
			      enum pdo_source_t source,
			      enum pdo_type_t pdo_type)
{
	if (!((source == LPM_PDO || source == PARTNER_PDO) &&
	      (pdo_type == SOURCE_PDO || pdo_type == SINK_PDO))) {
		return NULL;
	}

	if (source == LPM_PDO) {
		return (pdo_type == SOURCE_PDO) ? pdos->src_pdos :
						  pdos->snk_pdos;
	} else {
		return (pdo_type == SOURCE_PDO) ? pdos->partner_src_pdos :
						  pdos->partner_snk_pdos;
	}
}

static bool is_epr_pdo(uint32_t pdo)
{
	uint32_t type = PDO_GET_TYPE(pdo);

	return (type == PDO_GET_TYPE(PDO_TYPE_AUGMENTED) &&
		PDO_AUG_GET_PPS(pdo) == PDO_AUG_PPS_EPR) ||
	       (type == PDO_GET_TYPE(PDO_TYPE_FIXED) &&
		(pdo & PDO_FIXED_EPR_MODE_CAPABLE) != 0);
}

int emul_pdc_pdo_reset(struct emul_pdc_pdo_t *pdos)
{
	memset(pdos, 0, sizeof(struct emul_pdc_pdo_t));

	pdos->src_pdos[0] = EMUL_PDO_FIXED1_SRC;
	pdos->src_pdos[1] = EMUL_PDO_FIXED2_SRC;

	pdos->snk_pdos[0] = EMUL_PDO_FIXED_SNK;
	pdos->snk_pdos[1] = EMUL_PDO_BATT_SNK;
	pdos->snk_pdos[2] = EMUL_PDO_VAR_SNK;

	return 0;
}

int emul_pdc_pdo_get_direct(struct emul_pdc_pdo_t *data,
			    enum pdo_type_t pdo_type,
			    enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			    enum pdo_source_t source, uint32_t *pdos)
{
	const uint32_t *target_pdos = get_pdo_data(data, source, pdo_type);

	if (pdo_offset + num_pdos > PDO_OFFSET_MAX) {
		LOG_ERR("GET PDO offset overflow at %d, num pdos: %d",
			pdo_offset, num_pdos);
		return -EINVAL;
	}

	pdo_offset = MIN(pdo_offset, PDO_OFFSET_MAX);
	memcpy(pdos, &target_pdos[pdo_offset], num_pdos * sizeof(uint32_t));
	return 0;
}

int emul_pdc_pdo_set_direct(struct emul_pdc_pdo_t *data,
			    enum pdo_type_t pdo_type,
			    enum pdo_offset_t pdo_offset, uint8_t num_pdos,
			    enum pdo_source_t source, const uint32_t *pdos)
{
	uint32_t *target_pdos = get_pdo_data(data, source, pdo_type);

	if (!target_pdos) {
		return -EINVAL;
	}

	if (pdo_offset + num_pdos > PDO_OFFSET_MAX) {
		LOG_ERR("PDO offset overflow at %d, num pdos: %d", pdo_offset,
			num_pdos);
		return -EINVAL;
	}

	for (uint8_t i = 0; i < num_pdos; i++) {
		/* EPR PDOs are only supported in offsets 1-4. */
		if (is_epr_pdo(pdos[i]) &&
		    pdo_offset + i > EMUL_PDO_MAX_EPR_PDO_OFFSET) {
			LOG_ERR("Only PDOs 1-4 support EPR");
			return -EINVAL;
		}
	}

	memcpy(&target_pdos[pdo_offset], pdos, sizeof(uint32_t) * num_pdos);

	/* By default, if the test sets the partner sink PDOs, also update
	 * the partner RDO to match the fixed PDO.
	 */
	if (pdo_offset == 0 && source == PARTNER_PDO && pdo_type == SINK_PDO) {
		int max_curr = PDO_FIXED_GET_CURR(target_pdos[0]);
		data->partner_rdo = RDO_FIXED(1, max_curr, 500, 0);
	}

	/* TODO b/317065172: handle renegotiation if we have a port partner. */
	return 0;
}
