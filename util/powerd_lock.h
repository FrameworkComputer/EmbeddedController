/*
 * This file is ported from the flashrom project.
 *
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * powerd_lock.h: header file for power management routines
 */

#ifndef __UTIL_POWERD_LOCK_H
#define __UTIL_POWERD_LOCK_H 1

enum POWERD_ERROR_CODE {
	POWERD_OK = 0,
	POWERD_CREATE_LOCK_FILE_ERROR = 0x1,
	POWERD_WRITE_LOCK_FILE_ERROR  = 0x2,
	POWERD_CLOSE_LOCK_FILE_ERROR  = 0x4,
	POWERD_DELETE_LOCK_FILE_ERROR = 0x8
};

/* Disable power management. */
int disable_power_management(void);

/* Re-enable power management. */
int restore_power_management(void);

#endif	/* __UTIL_POWERD_LOCK_H */
