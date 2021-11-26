/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Corsola daughter board detection */

#ifndef __CROS_EC_CORSOLA_DB_DETECTION_H
#define __CROS_EC_CORSOLA_DB_DETECTION_H

enum corsola_db_type {
	CORSOLA_DB_NONE = -1,
	CORSOLA_DB_TYPEC,
	CORSOLA_DB_HDMI,
	CORSOLA_DB_COUNT,
};

#ifdef CONFIG_VARIANT_CORSOLA_DB_DETECTION
/*
 * Get the connected daughterboard type.
 *
 * @return		The daughterboard type.
 */
enum corsola_db_type corsola_get_db_type(void);
#else
inline enum corsola_db_type corsola_get_db_type(void)
{
	return CORSOLA_DB_NONE;
};
#endif /* CONFIG_VARIANT_CORSOLA_DB_DETECTION */

#endif /* __CROS_EC_CORSOLA_DB_DETECTION_H */
