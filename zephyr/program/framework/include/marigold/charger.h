/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CHARGER_H__
#define __CHARGER_H__

/**
 * Control ACOK by active charger port voltage
 *
 * @param voltage			active port voltage
 */
void acok_control(int voltage);

#endif /* __CHARGER_H__ */
