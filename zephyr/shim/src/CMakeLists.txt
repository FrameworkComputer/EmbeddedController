# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

zephyr_library_sources(console.c)
zephyr_library_sources(crc.c)
zephyr_library_sources(gpio.c)
zephyr_library_sources(util.c)

if (DEFINED CONFIG_ARCH_POSIX)
  zephyr_library_sources(ztest_system.c)
else()
  zephyr_library_sources(system.c)
  zephyr_library_sources("${PLATFORM_EC}/common/system.c")
endif()
zephyr_library_sources_ifdef(no_libgcc                  libgcc_${ARCH}.S)

zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ADC         adc.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_EEPROM  cbi.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_CBI_GPIO    cbi.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_ESPI        espi.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FAN         fan.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_FLASH_CROS  flash.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOOKS       hooks.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD     host_command.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_HOSTCMD_CONSOLE
                                                            console_buffer.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD    keyboard_raw.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_KEYBOARD    keyscan.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MKBP_EVENT  mkbp_event.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_MOTIONSENSE
                                                            motionsense_sensors.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PANIC       panic.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_PWM         pwm.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_RTC         rtc.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TEMP_SENSOR temp_sensors.c
                                                            thermal.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_TIMER       hwtimer.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_I2C         i2c.c)
zephyr_library_sources_ifdef(CONFIG_SHIMMED_TASKS           tasks.c)
zephyr_library_sources_ifdef(CONFIG_PLATFORM_EC_WATCHDOG    watchdog.c)
