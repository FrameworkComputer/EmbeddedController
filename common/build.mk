# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Common files build
#

common-y=main.o util.o console.o vboot.o uart_buffering.o
common-y+=memory_commands.o shared_mem.o system_common.o
common-y+=gpio_commands.o version.o
common-$(CONFIG_BATTERY_ATL706486)+=battery_atl706486.o
common-$(CONFIG_CHARGER_BQ24725)+=charger_bq24725.o
common-$(CONFIG_FLASH)+=flash_commands.o
common-$(CONFIG_LIGHTBAR)+=leds.o
common-$(CONFIG_LPC)+=port80.o host_event_commands.o
common-$(CONFIG_POWER_LED)+=power_led.o
common-$(CONFIG_PSTORE)+=pstore_commands.o
common-$(CONFIG_PWM)+=pwm_commands.o
common-$(CONFIG_SMART_BATTERY)+=smart_battery.o charge_state.o \
                                battery_commands.o
common-$(CONFIG_TASK_GAIAPOWER)+=gaia_power.o
common-$(CONFIG_TASK_HOSTCMD)+=host_command.o
common-$(CONFIG_TASK_I8042CMD)+=i8042.o keyboard.o
common-$(CONFIG_TASK_TEMPSENSOR)+=temp_sensor.o temp_sensor_commands.o
common-$(CONFIG_TASK_THERMAL)+=thermal.o thermal_commands.o
common-$(CONFIG_TASK_X86POWER)+=x86_power.o
common-$(CONFIG_TMP006)+=tmp006.o
common-$(CONFIG_USB_CHARGE)+=usb_charge.o usb_charge_commands.o
