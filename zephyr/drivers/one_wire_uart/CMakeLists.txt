# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ONE_WIRE_UART one_wire_uart.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ONE_WIRE_UART_TABLET
				tablet.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ONE_WIRE_UART_KEYBOARD
				keyboard.c)
