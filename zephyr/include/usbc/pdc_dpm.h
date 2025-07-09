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
 * @brief Get the source current that is provided by the PDC port.
 *
 * @param port USBC port number
 *
 * @returns The source current provided on the port in mA.
 */
int pdc_dpm_get_source_current(const int port);

#endif /* __CROS_EC_PDC_DPM_H */
