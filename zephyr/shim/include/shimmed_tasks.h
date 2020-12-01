/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_SHIMMED_TASKS_H
#define __CROS_EC_SHIMMED_TASKS_H

#ifdef CONFIG_PLATFORM_EC_POWERSEQ
#define HAS_TASK_CHIPSET 1
#endif /* CONFIG_PLATFORM_EC_POWERSEQ */

#ifdef CONFIG_PLATFORM_EC_HOSTCMD
#define HAS_TASK_HOSTCMD 1
#define CONFIG_HOSTCMD_EVENTS
#endif /* CONFIG_PLATFORM_EC_HOSTCMD */

#ifdef CONFIG_PLATFORM_EC_KEYBOARD
#define HAS_TASK_KEYSCAN 1
#endif /* CONFIG_PLATFORM_EC_KEYBOARD */

#ifdef CONFIG_HAS_TASK_KEYPROTO
#define HAS_TASK_KEYPROTO 1
#endif /* CONFIG_HAS_TASK_KEYPROTO */

#ifdef CONFIG_HAS_TASK_POWERBTN
#define HAS_TASK_POWERBTN 1
#endif /* CONFIG_HAS_TASK_POWERBTN */

#endif /* __CROS_EC_SHIMMED_TASKS_H */
