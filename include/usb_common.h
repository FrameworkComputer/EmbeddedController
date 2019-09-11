/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USB_COMMON_H
#define __CROS_EC_USB_COMMON_H

/* Functions that are shared between old and new PD stacks */
#include "usb_pd.h"
#include "usb_pd_tcpm.h"

/* Returns the battery percentage [0-100] of the system. */
int usb_get_battery_soc(void);

/*
 * Returns type C current limit (mA), potentially with the DTS flag, based upon
 * states of the CC lines on the partner side.
 *
 * @param polarity 0 if cc1 is primary, otherwise 1
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @return current limit (mA) with DTS flag set if appropriate
 */
typec_current_t usb_get_typec_current_limit(enum pd_cc_polarity_type polarity,
	enum tcpc_cc_voltage_status cc1, enum tcpc_cc_voltage_status cc2);

/**
 * Returns the polarity of a Sink.
 *
 * @param cc1 value of CC1 set by tcpm_get_cc
 * @param cc2 value of CC2 set by tcpm_get_cc
 * @return 0 if cc1 is primary, else 1 for cc2 being primary
 */
enum pd_cc_polarity_type get_snk_polarity(enum tcpc_cc_voltage_status cc1,
	enum tcpc_cc_voltage_status cc2);

/**
 * Find PDO index that offers the most amount of power and stays within
 * max_mv voltage.
 *
 * @param src_cap_cnt
 * @param src_caps
 * @param max_mv maximum voltage (or -1 if no limit)
 * @param pdo raw pdo corresponding to index, or index 0 on error (output)
 * @return index of PDO within source cap packet
 */
int pd_find_pdo_index(uint32_t src_cap_cnt, const uint32_t * const src_caps,
	int max_mv, uint32_t *selected_pdo);

/**
 * Extract power information out of a Power Data Object (PDO)
 *
 * @param pdo raw pdo to extract
 * @param ma current of the PDO (output)
 * @param mv voltage of the PDO (output)
 */
void pd_extract_pdo_power(uint32_t pdo, uint32_t *ma, uint32_t *mv);

/**
 * Decide which PDO to choose from the source capabilities.
 *
 * @param src_cap_cnt
 * @param src_caps
 * @param rdo  requested Request Data Object.
 * @param ma  selected current limit (stored on success)
 * @param mv  selected supply voltage (stored on success)
 * @param req_type request type
 * @param max_request_mv max voltage a sink can request before getting
 *			source caps
 */
void pd_build_request(uint32_t src_cap_cnt, const uint32_t * const src_caps,
	int32_t vpd_vdo, uint32_t *rdo, uint32_t *ma, uint32_t *mv,
	enum pd_request_type req_type, uint32_t max_request_mv);

/**
 * Notifies a task that is waiting on a system jump, that it's complete.
 *
 * @param sysjump_task_waiting  indicates if the task is waiting on the
 *				system jump.
 */
void notify_sysjump_ready(volatile const task_id_t * const
	sysjump_task_waiting);
#endif /* __CROS_EC_USB_COMMON_H */
