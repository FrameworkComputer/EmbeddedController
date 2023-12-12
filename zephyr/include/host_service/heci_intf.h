/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * ******** host bsp shim api definitions ********
 * these API should be implemented by shim drivers
 */

#ifndef _HECI_BSP_H_
#define _HECI_BSP_H_

#include "heci.h"

#define HECI_INTF_DEFINE(p) \
	static __in_section(heci, desc_##p, 0) __used __aligned(1)

typedef int (*bsp_read_host_msg_f)(uint32_t *drbl, uint8_t *msg,
				   uint32_t msg_size);

typedef int (*bsp_send_host_msg_f)(uint32_t drbl, uint8_t *msg,
				   uint32_t msg_size);

typedef int (*bsp_send_host_msg_poll_f)(uint32_t drbl, uint8_t *msg,
					uint32_t msg_size);

typedef int (*bsp_init)(void);
typedef int (*bsp_ack_host_f)(void);

typedef void (*set_ready_f)(uint32_t is_ready);

struct heci_bsp_t {
	uint16_t max_fragment_size;
	uint8_t core_id : 5;
	uint8_t peer_is_host : 1;
	uint8_t poll_write_support : 1;
	uint8_t mng_msg_support : 1;
	/* below is must-have interfaces */
	bsp_read_host_msg_f read_msg;
	bsp_send_host_msg_f send_msg;
	bsp_ack_host_f send_ack;
	bsp_init init;
	/* below is must-have interfaces while open poll_write_support */
	bsp_send_host_msg_poll_f poll_send_msg;
	/* below is nice-to-have interfaces */
	set_ready_f set_ready;
};

extern struct heci_bsp_t *host_intf;

extern struct heci_bsp_t __heci_desc_start;
extern struct heci_bsp_t __heci_desc_end;

int send_heci_newmsg_notify(struct heci_bsp_t *sender);

struct heci_bsp_t *wait_and_draw_heci_newmsg(void);

struct heci_bsp_t *heci_intf_get_entry(int core_id);

void dispatch_msg_to_core(struct heci_bsp_t *bsp_intf);

int host_svr_hal_init(void);

uint32_t get_heci_core_bitmap(void);

#endif
