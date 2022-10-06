/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __AP_POWER_AP_PWRSEQ_H__
#define __AP_POWER_AP_PWRSEQ_H__

/** Starts the AP power sequence thread */
void ap_pwrseq_task_start(void);

void ap_pwrseq_wake(void);
#endif /* __AP_POWER_AP_PWRSEQ_H__ */
