/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef __CROS_EC_USART_STM32F4_H
#define __CROS_EC_USART_STM32F4_H

#include "usart.h"

/*
 * The STM32F4 series can have as many as three UARTS.  These are the HW configs
 * for those UARTS.  They can be used to initialize STM32 generic UART configs.
 * CONFIG_STREAM_USART<X> enables the corresponding hardware instance.
 */
extern struct usart_hw_config const usart1_hw;
extern struct usart_hw_config const usart2_hw;
extern struct usart_hw_config const usart3_hw;

#endif /* __CROS_EC_USART_STM32F4_H */
