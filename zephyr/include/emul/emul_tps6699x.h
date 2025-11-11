/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EMUL_TPS6699X_H_
#define __EMUL_TPS6699X_H_

#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc_pdo.h"
#include "include/usb_pd.h"
#include "tps6699x_reg.h"

#include <stdint.h>

#include <zephyr/drivers/gpio.h>

#define TPS6699X_MAX_REG 0xa4
#define TPS6699X_REG_SIZE 64

/* Size of bitmap using uint32_t for all TPS registers. 256 / 32 = 8 bytes. */
#define REG_U32_BITMAP_SIZE 8

struct ti_ccom {
	uint16_t connector_number : 7;
	uint16_t cc_operation_mode : 3;
	uint16_t reserved : 6;
} __packed;

struct ti_get_pdos {
	uint8_t connector_number : 7;
	uint8_t partner_pdo : 1;
	uint8_t pdo_offset : 8;
	uint8_t num_pdos : 2;
	uint8_t source : 1;
	uint8_t source_caps : 2;
	uint8_t reserved : 1;
} __packed;

enum switch_select {
	PP_5V1 = 0,
	PP_5V2 = 1,
	PP_EXT1 = 2,
	PP_EXT2 = 3,
};

struct ti_task_srdy {
	uint8_t switch_select : 3;
	uint8_t reserved : 5;
} __packed;

struct tps6699x_response {
	uint8_t result : 4;
	uint8_t reserved : 4;
	union {
		struct {
			uint8_t length;
			union {
				union error_status_t error;
				struct ti_ccom ccom;
				uint32_t pdos[4];
				uint32_t pd_message[PDC_DISC_IDENTITY_VDO_COUNT];
			};
		} __packed;
		union connector_status_t connector_status;
		struct capability_t capability;
		union connector_capability_t connector_capability;
		union cable_property_t cable_property;
		uint32_t current_cam;
	} data;
} __packed;

struct tps6699x_emul_pdc_data {
	struct gpio_dt_spec irq_gpios;
	uint32_t delay_ms;
	struct k_work_delayable delay_work;
	/* The register address currently being read or written. */
	uint8_t reg_addr;
	/* The stated length of the current read or write. */
	uint8_t transaction_bytes;
	/* There are 0xa4 registers, and the biggest is 512 bits long.
	 * TODO(b/345292002): Define a real data structure for registers.
	 */
	uint8_t reg_val[TPS6699X_MAX_REG][TPS6699X_REG_SIZE];

	union connector_status_t connector_status;
	union connector_reset_t reset_cmd;
	union error_status_t error;
	struct capability_t capability;
	union connector_capability_t connector_capability;
	union uor_t uor;
	union pdr_t pdr;
	enum ccom_t ccom;
	union cable_property_t cable_property;
	union reg_port_control port_control;
	bool frs_configured;
	uint32_t rmdo;
	uint32_t identity[PDC_DISC_IDENTITY_VDO_COUNT];
	uint32_t current_cam;

	struct tps6699x_response response;

	uint32_t fail_reg_reads[REG_U32_BITMAP_SIZE];
	uint32_t fail_reg_writes[REG_U32_BITMAP_SIZE];

	int fail_next_ucsi_cmd_count;
	enum ucsi_command_t fail_next_ucsi_cmd;
	enum std_task_response fail_next_ucsi_cmd_with_response;

	uint32_t pending_rdo;
	struct emul_pdc_pdo_t pdo;
	bool cmd_error;
	struct k_work_delayable aneg_delay_work;
	struct k_work_delayable delayed_sink_contract_negotiation_work;
	/** PDC feature flags */
	ATOMIC_DEFINE(features, EMUL_PDC_FEATURE_COUNT);
};

/* Fail next register read from given reg. */
int emul_pdc_fail_reg_read(const struct emul *target, uint8_t reg);

/* Fail next register write to given reg. */
int emul_pdc_fail_reg_write(const struct emul *target, uint8_t reg);

/* With the next UCSI command sent, fail with the provided response. */
int emul_pdc_fail_next_ucsi_command(const struct emul *target,
				    enum ucsi_command_t command,
				    enum std_task_response with_response,
				    uint8_t num_times);

/* Set the patch loaded interrupt bit. This will cause the driver to re-init all
 * ports.
 */
int emul_pdc_set_interrupt_patch_loaded(const struct emul *target);

#endif /* __EMUL_TPS6699X_H_ */
