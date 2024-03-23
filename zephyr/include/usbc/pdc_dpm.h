/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * @file
 * @brief Device Policy Manager for PD Controllers
 */

#ifndef __CROS_EC_PDC_DPM_H
#define __CROS_EC_PDC_DPM_H

/**
 * @brief Evaluate port's first SNK_CAP PDO for current consideration
 *
 * @param port USBC port number
 * @param vsafe5v_pdo First PDO of port partner's SNK_CAPs
 */
void pdc_dpm_eval_sink_fixed_pdo(int port, uint32_t vsafe5v_pdo);

/**
 * @brief Add typec-only port to max current request
 *
 * @param port USBC port number
 */
void pdc_dpm_add_non_pd_sink(int port);

/**
 * @brief Remove port from max current request
 *
 * @param port USBC port number
 */
void pdc_dpm_remove_sink(int port);

/**
 * @brief Remove port from max current request
 *
 * @param port USBC port number
 */
void pdc_dpm_remove_source(int port);

/**
 * @brief Check current requested from port partner
 *
 * @param port USBC port number
 * @param rdo Requested Data Object from port partner
 */
void pdc_dpm_evaluate_request_rdo(int port, uint32_t rdo);

#endif /* __CROS_EC_PDC_DPM_H */
