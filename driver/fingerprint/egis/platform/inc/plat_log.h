/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_LOG_H_
#define __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_LOG_H_

#include "console.h"

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	LOG_VERBOSE = 2,
	LOG_DEBUG = 3,
	LOG_INFO = 4,
	LOG_WARN = 5,
	LOG_ERROR = 6,
	LOG_ASSERT = 7,
} LOG_LEVEL;

#define EGIS_LOG_ENTRY() egislog_d("Start %s", __func__)
#define EGIS_LOG_EXIT(x) egislog_i("Exit %s, ret=%d", __func__, x)

#define FILE_NAME \
	(strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define egislog(level, format, ...)                                       \
	output_log(level, LOG_TAG, FILE_NAME, __func__, __LINE__, format, \
		   ##__VA_ARGS__)
#define ex_log(level, format, ...)                                      \
	output_log(level, "RBS", FILE_NAME, __func__, __LINE__, format, \
		   ##__VA_ARGS__)

#if !defined(LOGD)
#define LOGD(format, ...)                                                   \
	output_log(LOG_DEBUG, "RBS", FILE_NAME, __func__, __LINE__, format, \
		   ##__VA_ARGS__)
#endif

#if !defined(LOGE)
#define LOGE(format, ...)                                                   \
	output_log(LOG_ERROR, "RBS", FILE_NAME, __func__, __LINE__, format, \
		   ##__VA_ARGS__)
#endif

void output_log(LOG_LEVEL level, const char *tag, const char *file_name,
		const char *func, int line, const char *format, ...);

void set_debug_level(LOG_LEVEL level);
#if defined(SDK_EVTOOL_DEBUG) || defined(SDK_ALGO_MODULE_MODE)
#include "common_definition.h"
void set_debug_log_callback(event_callback_t event_callback);
#endif

#define egislog_e(format, args...) egislog(LOG_ERROR, format, ##args)
#define egislog_d(format, args...) egislog(LOG_DEBUG, format, ##args)
#define egislog_i(format, args...) egislog(LOG_INFO, format, ##args)
#define egislog_v(format, args...) egislog(LOG_VERBOSE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_FP, format, ##args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ##args)

#define RBS_CHECK_IF_NULL(x, errorcode)               \
	if (x == NULL) {                              \
		LOGE("%s, " #x " is NULL", __func__); \
		return errorcode;                     \
	}

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_DRIVER_FINGERPRINT_EGIS_PLATFORM_INC_PLAT_LOG_H_ */
