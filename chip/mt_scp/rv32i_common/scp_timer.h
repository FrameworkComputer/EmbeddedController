/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __SCP_TIMER_H
#define __SCP_TIMER_H

#define TIMER_EVENT 3
#define TIMER_SR 4 /* detect existance of 26m clock in S3 stage */
#define TIMER_SYSTEM 5

void timer_enable(int n);
void timer_disable(int n);
uint32_t timer_read_raw_sr(void);

#endif /* __SCP_TIMER_H */
