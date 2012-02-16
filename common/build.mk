# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Common files build
#

common-y=main.o util.o console.o vboot.o uart_buffering.o
common-y+=memory_commands.o shared_mem.o system.o usb_charge.o
common-y+=gpio_commands.o
common-$(CONFIG_LPC)+=port80.o
common-$(CONFIG_TASK_HOSTCMD)+=host_command.o
common-$(CONFIG_TASK_I8042CMD)+=i8042.o keyboard.o
common-$(CONFIG_TASK_X86POWER)+=x86_power.o
common-$(CONFIG_TASK_GAIAPOWER)+=gaia_power.o
common-$(CONFIG_FLASH)+=flash_commands.o
common-$(CONFIG_PWM)+=pwm_commands.o
common-$(CONFIG_TEMP_SENSOR)+=temp_sensor.o temp_sensor_commands.o
common-$(CONFIG_LIGHTBAR)+=leds.o

# Board driver modules
common-$(CONFIG_CHARGER_BQ24725)+=charger_bq24725.o
common-$(CONFIG_SMART_BATTERY)+=smart_battery.o
