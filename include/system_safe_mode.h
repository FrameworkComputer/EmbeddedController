/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SYSTEM_SAFE_MODE_H
#define __CROS_EC_SYSTEM_SAFE_MODE_H

#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Checks if running in system safe mode
 *
 * @return True if system is running in system safe mode
 */
bool system_is_in_safe_mode(void);

/**
 * Checks if command is allowed in system safe mode
 *
 * @return True if command is allowed in system safe mode
 */
bool command_is_allowed_in_safe_mode(int command);

/**
 * Checks if a task is critical for system safe mode
 *
 * @return True if task is safe mode critical
 */
bool is_task_safe_mode_critical(task_id_t task_id);

/**
 * Disables tasks that are not critical for safe mode
 *
 * @return EC_SUCCESS or EC_xxx on error
 */
int disable_non_safe_mode_critical_tasks(void);

/**
 * Start system safe mode.
 *
 * System safe mode can only be started after a panic in RW image.
 * It will only run briefly so the AP can capture EC state.
 *
 * @return EC_SUCCESS or EC_xxx on error
 */
int start_system_safe_mode(void);

/**
 * This handler is called when safe mode times out.
 */
void handle_system_safe_mode_timeout(void);

#ifdef TEST_BUILD
/**
 * Directly set safe mode flag. Only used in tests.
 */
void set_system_safe_mode(bool mode);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_SYSTEM_SAFE_MODE_H */
