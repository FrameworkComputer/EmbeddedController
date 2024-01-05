# Copyright 2022 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources(
	"src/usb_mux_config.c"
	"src/kb_backlight.c"
	"src/sensor.c"
	"src/battery_present.c"
	"src/alt_charger.c"
	"src/thermal.c"
)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CUSTOM_FAN_DUTY_CONTROL
  "src/fan.c")
