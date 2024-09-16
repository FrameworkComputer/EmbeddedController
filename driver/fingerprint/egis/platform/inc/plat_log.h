/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __PLAT_LOG_H_
#define __PLAT_LOG_H_

#include "console.h"

#include <string.h>

#if defined(TZ_MODE) && defined(__TRUSTONIC__)
#define malloc malloc_NOT_SUPPORTED
#define sprintf sprintf_NOT_SUPPORTED
#define vsnprintf vsnprintf_NOT_SUPPORTED
#define snprintf snprintf_NOT_SUPPORTED
#endif

typedef enum {
	LOG_VERBOSE = 2,
	LOG_DEBUG = 3,
	LOG_INFO = 4,
	LOG_WARN = 5,
	LOG_ERROR = 6,
	LOG_ASSERT = 7,
} LOG_LEVEL;

#ifdef _MSC_VER
#ifndef __func__
#define __func__ __FUNCTION__
#endif
#endif

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

#ifdef __cplusplus
extern "C" {
#endif
void output_log(LOG_LEVEL level, const char *tag, const char *file_name,
		const char *func, int line, const char *format, ...);

void set_debug_level(LOG_LEVEL level);
#if defined(SDK_EVTOOL_DEBUG) || defined(SDK_ALGO_MODULE_MODE)
#include "common_definition.h"
void set_debug_log_callback(event_callback_t event_callback);
#endif

#ifdef __cplusplus
}
#endif

#ifdef _WINDOWS
#define egislog_e(format, ...) egislog(LOG_ERROR, format, ##__VA_ARGS__)
#define egislog_d(format, ...) egislog(LOG_DEBUG, format, ##__VA_ARGS__)
#define egislog_i(format, ...) egislog(LOG_INFO, format, ##__VA_ARGS__)
#define egislog_v(format, ...) egislog(LOG_VERBOSE, format, ##__VA_ARGS__)
#define CPRINTF(format, ...) cprintf(CC_FP, format, ##__VA_ARGS__)
#define CPRINTS(format, ...) cprints(CC_FP, format, ##__VA_ARGS__)
#else
#define egislog_e(format, args...) egislog(LOG_ERROR, format, ##args)
#define egislog_d(format, args...) egislog(LOG_DEBUG, format, ##args)
#define egislog_i(format, args...) egislog(LOG_INFO, format, ##args)
#define egislog_v(format, args...) egislog(LOG_VERBOSE, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_FP, format, ##args)
#define CPRINTS(format, args...) cprints(CC_FP, format, ##args)
#endif

#define RBS_CHECK_IF_NULL(x, errorcode)               \
	if (x == NULL) {                              \
		LOGE("%s, " #x " is NULL", __func__); \
		return errorcode;                     \
	}
#endif