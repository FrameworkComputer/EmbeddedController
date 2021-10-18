/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_COMMON_USB_PD_PDO_H
#define __CROS_EC_COMMON_USB_PD_PDO_H

/* ---------------- Power Data Objects (PDOs) ----------------- */
#ifndef CONFIG_USB_PD_CUSTOM_PDO
extern const uint32_t pd_src_pdo[1];
extern const int pd_src_pdo_cnt;
extern const uint32_t pd_src_pdo_max[1];
extern const int pd_src_pdo_max_cnt;
extern const uint32_t pd_snk_pdo[3];
extern const int pd_snk_pdo_cnt;
#endif /* CONFIG_USB_PD_CUSTOM_PDO */

#endif /* __CROS_EC_COMMON_USB_PD_PDO_H */
