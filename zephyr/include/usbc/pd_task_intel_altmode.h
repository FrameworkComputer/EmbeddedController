/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * @file
 * @brief PD task to configure USB-C Alternate modes on Intel SoC.
 */

#ifndef __CROS_EC_PD_TASK_INTEL_ALTMODE_H
#define __CROS_EC_PD_TASK_INTEL_ALTMODE_H

/**
 * @brief Starts the Intel Alternate Mode configuration thread.
 */
void intel_altmode_task_start(void);

/*
 * @brief Suspend pd_intel_altmode_task.
 */
void suspend_pd_intel_altmode_task(void);

/*
 * @brief Resume pd_intel_altmode_task.
 */
void resume_pd_intel_altmode_task(void);

/*
 * @brief Check if the state of pd_intel_altmode_task is suspended.
 *
 * @return True if state of the pd_intel_altmode_task is suspended.
 *         Else false.
 */
bool is_pd_intel_altmode_task_suspended(void);

#endif /* __CROS_EC_PD_TASK_INTEL_ALTMODE_H */
