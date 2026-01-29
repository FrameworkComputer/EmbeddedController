# -*- makefile -*-
# Copyright 2013 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Device test binaries
test-list-y ?= stdlib

# Emulator tests
ifneq ($(TEST_LIST_HOST),)
test-list-host=$(TEST_LIST_HOST)
else
test-list-host =
test-list-host += battery_config
test-list-host += bklight_passthru
test-list-host += body_detection
test-list-host += boringssl_crypto
test-list-host += cbi
test-list-host += charge_ramp
test-list-host += console_edit
test-list-host += crc
test-list-host += fan
test-list-host += fpsensor_auth_commands
test-list-host += fpsensor_auth_commands_otp
test-list-host += fpsensor_auth_crypto_stateful
test-list-host += fpsensor_auth_crypto_stateless
test-list-host += fpsensor_crypto
test-list-host += fpsensor_crypto_with_mock
test-list-host += fpsensor_crypto_with_mock_otp
test-list-host += fpsensor_state
test-list-host += host_command
test-list-host += kb_8042
test-list-host += kb_scan
test-list-host += math_util
test-list-host += motion_angle
test-list-host += motion_angle_tablet
test-list-host += motion_lid
test-list-host += motion_sense_fifo
test-list-host += power_button
test-list-host += printf
test-list-host += queue
test-list-host += rgb_keyboard
test-list-host += rollback_secret
test-list-host += rsa3
test-list-host += rtc
test-list-host += sbs_charging
test-list-host += sha256
test-list-host += sha256_unrolled
# TODO(b/237823627): When building for the host, we're linking against the
# toolchain's C standard library, so these tests are actually testing the
# toolchain's C standard library.
test-list-host += stdlib
test-list-host += thermal
test-list-host += uptime
test-list-host += usb_pd_console
test-list-host += usb_pd_timer
test-list-host += usb_ppc
test-list-host += usb_sm_framework_h3
test-list-host += usb_typec_vpd
test-list-host += usb_typec_ctvpd
test-list-host += usb_typec_drp_acc_trysrc
test-list-host += usb_prl_old
test-list-host += usb_tcpmv2_compliance
test-list-host += usb_prl
test-list-host += usb_prl_noextended
test-list-host += usb_pe_drp_old
test-list-host += usb_pe_drp_old_noextended
test-list-host += usb_pe_drp
test-list-host += usb_pe_drp_noextended
test-list-host += vboot
test-list-host += x25519
endif

# Build up the list of coverage test targets based on test-list-host, but
# with some tests excluded because they cause code coverage to fail.

cov-dont-test =

cov-test-list-host = $(filter-out $(cov-dont-test), $(test-list-host))

rw-test = rw
ifeq ($(and $(BOARD_HOST),$(TEST_BUILD)),y)
# TODO(b/346616972): The "emulator" (TEST_BUILD=y with BOARD=host) runs the
# tests from the RO image, so we need to build for RO.
rw-test = ro
endif

battery_config-y=battery_config.o
bklight_passthru-y=bklight_passthru.o
body_detection-y=body_detection.o body_detection_data_literals.o motion_common.o
boringssl_crypto-y=boringssl_crypto.o
cbi-y=cbi.o
charge_ramp-y+=charge_ramp.o
console_edit-y=console_edit.o
crc-y=crc.o
fan-y=fan.o
fpsensor_auth_commands-y=fpsensor_auth_commands.o
fpsensor_auth_commands_otp-$(rw-test)=fpsensor_auth_commands_otp.o
fpsensor_auth_crypto_stateful-y=fpsensor_auth_crypto_stateful.o
fpsensor_auth_crypto_stateless-y=fpsensor_auth_crypto_stateless.o
fpsensor_crypto-y=fpsensor_crypto.o
fpsensor_crypto_with_mock-y=fpsensor_crypto_with_mock.o
fpsensor_crypto_with_mock_otp-y=fpsensor_crypto_with_mock_otp.o
fpsensor_state-y=fpsensor_state.o
host_command-y=host_command.o
kb_8042-y=kb_8042.o
kb_scan-y=kb_scan.o
math_util-y=math_util.o
motion_angle-y=motion_angle.o motion_angle_data_literals.o motion_common.o
motion_angle_tablet-y=motion_angle_tablet.o motion_angle_data_literals_tablet.o motion_common.o
motion_lid-y=motion_lid.o
motion_sense_fifo-y=motion_sense_fifo.o
rgb_keyboard-y=rgb_keyboard.o
power_button-y=power_button.o
printf-y=printf.o
queue-y=queue.o
rollback_secret-y=rollback_secret.o
rsa3-y=rsa.o
rtc-y=rtc.o
sbs_charging-y=sbs_charging.o
sha256-y=sha256.o
sha256_unrolled-y=sha256.o
stdlib-y=stdlib.o
thermal-y=thermal.o
uptime-y=uptime.o
usb_pd_console-y=usb_pd_console.o
usb_pd_timer-y=usb_pd_timer.o
usb_ppc-y=usb_ppc.o
usb_sm_framework_h3-y=usb_sm_framework_h3.o
usb_typec_vpd-y=usb_typec_ctvpd.o vpd_api.o usb_sm_checks.o fake_usbc.o
usb_typec_ctvpd-y=usb_typec_ctvpd.o vpd_api.o usb_sm_checks.o fake_usbc.o
usb_typec_drp_acc_trysrc-y=usb_typec_drp_acc_trysrc.o vpd_api.o \
	usb_sm_checks.o
usb_prl_old-y=usb_prl_old.o usb_sm_checks.o fake_usbc.o
usb_prl-y=usb_prl.o usb_sm_checks.o
usb_prl_noextended-y=usb_prl_noextended.o usb_sm_checks.o fake_usbc.o
usb_pe_drp_old-y=usb_pe_drp_old.o usb_sm_checks.o
usb_pe_drp_old_noextended-y=usb_pe_drp_old_noextended.o usb_sm_checks.o
usb_pe_drp-y=usb_pe_drp.o usb_sm_checks.o
usb_pe_drp_noextended-y=usb_pe_drp_noextended.o usb_sm_checks.o
usb_tcpmv2_compliance-y=usb_tcpmv2_compliance.o usb_tcpmv2_compliance_common.o \
	usb_tcpmv2_td_pd_ll_e3.o \
	usb_tcpmv2_td_pd_ll_e4.o \
	usb_tcpmv2_td_pd_ll_e5.o \
	usb_tcpmv2_td_pd_src_e1.o \
	usb_tcpmv2_td_pd_src_e2.o \
	usb_tcpmv2_td_pd_src_e5.o \
	usb_tcpmv2_td_pd_src3_e1.o \
	usb_tcpmv2_td_pd_src3_e7.o \
	usb_tcpmv2_td_pd_src3_e8.o \
	usb_tcpmv2_td_pd_src3_e9.o \
	usb_tcpmv2_td_pd_src3_e26.o \
	usb_tcpmv2_td_pd_src3_e32.o \
	usb_tcpmv2_td_pd_snk3_e12.o \
	usb_tcpmv2_td_pd_vndi3_e3.o \
	usb_tcpmv2_td_pd_other.o \
	test_battery_mock.o
vboot-y=vboot.o
x25519-y=x25519.o
