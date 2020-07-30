# -*- makefile -*-
# Copyright 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Device test binaries
test-list-y ?= flash_write_protect pingpong timer_calib timer_dos timer_jump mutex utils utils_str
#disable: powerdemo

# Emulator tests
ifneq ($(TEST_LIST_HOST),)
test-list-host=$(TEST_LIST_HOST)
else
test-list-host = accel_cal
test-list-host += aes
test-list-host += base32
test-list-host += battery_get_params_smart
test-list-host += bklight_lid
test-list-host += bklight_passthru
test-list-host += body_detection
test-list-host += button
test-list-host += cbi
test-list-host += cec
test-list-host += charge_manager
test-list-host += charge_manager_drp_charging
test-list-host += charge_ramp
test-list-host += compile_time_macros
test-list-host += console_edit
test-list-host += crc32
test-list-host += entropy
test-list-host += extpwr_gpio
test-list-host += fan
test-list-host += flash
test-list-host += float
test-list-host += fp
test-list-host += fpsensor
test-list-host += fpsensor_crypto
test-list-host += fpsensor_state
test-list-host += gyro_cal
test-list-host += hooks
test-list-host += host_command
test-list-host += i2c_bitbang
test-list-host += inductive_charging
test-list-host += interrupt
test-list-host += is_enabled
test-list-host += is_enabled_error
test-list-host += kasa
test-list-host += kb_8042
test-list-host += kb_mkbp
#test-list-host += kb_scan	# crbug.com/976974
test-list-host += lid_sw
test-list-host += lightbar
test-list-host += mag_cal
test-list-host += math_util
test-list-host += motion_angle
test-list-host += motion_angle_tablet
test-list-host += motion_lid
test-list-host += motion_sense_fifo
test-list-host += mutex
test-list-host += newton_fit
test-list-host += online_calibration
test-list-host += pingpong
test-list-host += power_button
test-list-host += printf
test-list-host += queue
test-list-host += rsa
test-list-host += rsa3
test-list-host += rtc
test-list-host += sbs_charging_v2
test-list-host += sha256
test-list-host += sha256_unrolled
test-list-host += shmalloc
test-list-host += static_if
test-list-host += static_if_error
test-list-host += system
test-list-host += thermal
test-list-host += timer_dos
test-list-host += uptime
test-list-host += usb_common
test-list-host += usb_pd_int
test-list-host += usb_pd
test-list-host += usb_pd_giveback
test-list-host += usb_pd_rev30
test-list-host += usb_ppc
test-list-host += usb_sm_framework_h3
test-list-host += usb_sm_framework_h2
test-list-host += usb_sm_framework_h1
test-list-host += usb_sm_framework_h0
test-list-host += usb_typec_vpd
test-list-host += usb_typec_ctvpd
test-list-host += usb_typec_drp_acc_trysrc
test-list-host += usb_prl_old
test-list-host += usb_tcpmv2_tcpci
test-list-host += usb_prl
test-list-host += usb_prl_noextended
test-list-host += usb_pe_drp_old
test-list-host += usb_pe_drp_old_noextended
test-list-host += usb_pe_drp
test-list-host += usb_pe_drp_noextended
test-list-host += utils
test-list-host += utils_str
test-list-host += vboot
test-list-host += x25519
test-list-host += stillness_detector
endif

# Build up the list of coverage test targets based on test-list-host, but
# with some tests excluded because they cause code coverage to fail.

# is_enabled_error is a shell script that does not produce coverage results
cov-dont-test = is_enabled_error
# static_if_error is a shell script that does not produce coverage results
cov-dont-test += static_if_error
# fpsensor: genhtml looks for build/host/fpsensor/cryptoc/util.c
cov-dont-test += fpsensor
# fpsensor_crypto: genhtml looks for build/host/fpsensor_crypto/cryptoc/util.c
cov-dont-test += fpsensor_crypto
# fpsensor_state: genhtml looks for build/host/fpsensor_state/cryptoc/util.c
cov-dont-test += fpsensor_state
cov-test-list-host = $(filter-out $(cov-dont-test), $(test-list-host))

accel_cal-y=accel_cal.o
aes-y=aes.o
base32-y=base32.o
battery_get_params_smart-y=battery_get_params_smart.o
bklight_lid-y=bklight_lid.o
bklight_passthru-y=bklight_passthru.o
body_detection-y=body_detection.o body_detection_data_literals.o motion_common.o
button-y=button.o
cbi-y=cbi.o
cec-y=cec.o
charge_manager-y=charge_manager.o
charge_manager_drp_charging-y=charge_manager.o
charge_ramp-y+=charge_ramp.o
compile_time_macros-y=compile_time_macros.o
console_edit-y=console_edit.o
crc32-y=crc32.o
entropy-y=entropy.o
extpwr_gpio-y=extpwr_gpio.o
fan-y=fan.o
flash-y=flash.o
flash_physical-y=flash_physical.o
flash_write_protect-y=flash_write_protect.o
fpsensor-y=fpsensor.o
fpsensor_crypto-y=fpsensor_crypto.o
fpsensor_state-y=fpsensor_state.o
gyro_cal-y=gyro_cal.o
hooks-y=hooks.o
host_command-y=host_command.o
i2c_bitbang-y=i2c_bitbang.o
inductive_charging-y=inductive_charging.o
interrupt-y=interrupt.o
is_enabled-y=is_enabled.o
kb_8042-y=kb_8042.o
kb_mkbp-y=kb_mkbp.o
kb_scan-y=kb_scan.o
lid_sw-y=lid_sw.o
lightbar-y=lightbar.o
mag_cal-y=mag_cal.o
math_util-y=math_util.o
motion_angle-y=motion_angle.o motion_angle_data_literals.o motion_common.o
motion_angle_tablet-y=motion_angle_tablet.o motion_angle_data_literals_tablet.o motion_common.o
motion_lid-y=motion_lid.o
motion_sense_fifo-y=motion_sense_fifo.o
online_calibration-y=online_calibration.o
kasa-y=kasa.o
mpu-y=mpu.o
mutex-y=mutex.o
newton_fit-y=newton_fit.o
pingpong-y=pingpong.o
power_button-y=power_button.o
powerdemo-y=powerdemo.o
printf-y=printf.o
queue-y=queue.o
rollback-y=rollback.o
rollback_entropy-y=rollback_entropy.o
rsa-y=rsa.o
rsa3-y=rsa.o
rtc-y=rtc.o
scratchpad-y=scratchpad.o
sbs_charging-y=sbs_charging.o
sbs_charging_v2-y=sbs_charging_v2.o
sha256-y=sha256.o
sha256_unrolled-y=sha256.o
shmalloc-y=shmalloc.o
static_if-y=static_if.o
stm32f_rtc-y=stm32f_rtc.o
stress-y=stress.o
system-y=system.o
thermal-y=thermal.o
timer_calib-y=timer_calib.o
timer_dos-y=timer_dos.o
uptime-y=uptime.o
usb_common-y=usb_common_test.o fake_battery.o
usb_pd_int-y=usb_pd_int.o
usb_pd-y=usb_pd.o
usb_pd_giveback-y=usb_pd.o
usb_pd_rev30-y=usb_pd.o
usb_ppc-y=usb_ppc.o
usb_sm_framework_h3-y=usb_sm_framework_h3.o
usb_sm_framework_h2-y=usb_sm_framework_h3.o
usb_sm_framework_h1-y=usb_sm_framework_h3.o
usb_sm_framework_h0-y=usb_sm_framework_h3.o
usb_typec_vpd-y=usb_typec_ctvpd.o vpd_api.o usb_sm_checks.o fake_usbc.o
usb_typec_ctvpd-y=usb_typec_ctvpd.o vpd_api.o usb_sm_checks.o fake_usbc.o
usb_typec_drp_acc_trysrc-y=usb_typec_drp_acc_trysrc.o vpd_api.o \
	usb_sm_checks.o
usb_prl_old-y=usb_prl_old.o usb_sm_checks.o fake_usbc.o
usb_prl-y=usb_prl.o usb_sm_checks.o
usb_prl_noextended-y=usb_prl_noextended.o usb_sm_checks.o fake_usbc.o
usb_pe_drp_old-y=usb_pe_drp_old.o usb_sm_checks.o fake_usbc.o
usb_pe_drp_old_noextended-y=usb_pe_drp_old.o usb_sm_checks.o fake_usbc.o
usb_pe_drp-y=usb_pe_drp.o usb_sm_checks.o
usb_pe_drp_noextended-y=usb_pe_drp_noextended.o usb_sm_checks.o
usb_tcpmv2_tcpci-y=usb_tcpmv2_tcpci.o vpd_api.o usb_sm_checks.o
utils-y=utils.o
utils_str-y=utils_str.o
vboot-y=vboot.o
float-y=fp.o
fp-y=fp.o
x25519-y=x25519.o
stillness_detector-y=stillness_detector.o

host-is_enabled_error: TEST_SCRIPT=is_enabled_error.sh
is_enabled_error-y=is_enabled_error.o.cmd

host-static_if_error: TEST_SCRIPT=static_if_error.sh
static_if_error-y=static_if_error.o.cmd
