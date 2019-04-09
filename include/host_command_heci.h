/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __HOST_COMMAND_HECI_H
#define __HOST_COMMAND_HECI_H

/* send an event message to the ap */
int heci_send_mkbp_event(uint32_t *timestamp);

#endif /* __HOST_COMMAND_HECI_H */
