/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB endpoints/interfaces callbacks declaration
 */

#include "config.h"

.section .rodata.usb_ep

.macro endpoint number suffix
.if \number < USB_EP_COUNT
.long ep_\number\()_\()\suffix
.weak ep_\number\()_\()\suffix
.set ep_\number\()_\()\suffix, ep_undefined
.endif
.endm

.macro interface number
.if \number < USB_IFACE_COUNT
.long iface_\number\()_request
.weak iface_\number\()_request
.set iface_\number\()_request, iface_undefined
.endif
.endm

/* align function pointers on a 32-bit boundary */
.align 2
/* Endpoint callbacks */
.global usb_ep_tx
usb_ep_tx:
endpoint  0 tx
endpoint  1 tx
endpoint  2 tx
endpoint  3 tx
endpoint  4 tx
endpoint  5 tx
endpoint  6 tx
endpoint  7 tx
endpoint  8 tx
endpoint  9 tx
endpoint 10 tx
endpoint 11 tx
endpoint 12 tx
endpoint 13 tx
endpoint 14 tx
endpoint 15 tx

.global usb_ep_rx
usb_ep_rx:
endpoint  0 rx
endpoint  1 rx
endpoint  2 rx
endpoint  3 rx
endpoint  4 rx
endpoint  5 rx
endpoint  6 rx
endpoint  7 rx
endpoint  8 rx
endpoint  9 rx
endpoint 10 rx
endpoint 11 rx
endpoint 12 rx
endpoint 13 rx
endpoint 14 rx
endpoint 15 rx

.global usb_ep_reset
usb_ep_reset:
endpoint  0 rst
endpoint  1 rst
endpoint  2 rst
endpoint  3 rst
endpoint  4 rst
endpoint  5 rst
endpoint  6 rst
endpoint  7 rst
endpoint  8 rst
endpoint  9 rst
endpoint 10 rst
endpoint 11 rst
endpoint 12 rst
endpoint 13 rst
endpoint 14 rst
endpoint 15 rst

.global usb_iface_request
usb_iface_request:
interface  0
interface  1
interface  2
interface  3
interface  4
interface  5
interface  6
interface  7

.text
.code 16

.thumb_func
/* Undefined interface callbacks fail by returning non-zero*/
iface_undefined:
	mov r0, #1
ep_undefined:
	bx lr
