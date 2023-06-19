# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

target_sources(app PRIVATE
	adc.c
	battery.c
	cbi.c
	charge_manager.c
	charge_state.c
	charger.c
	chargen.c
	accelinit.c
	accelinfo.c
	accelspoof.c
	accelrate.c
	accelrange.c
	accelread.c
	accelres.c
	button.c
	cutoff.c
	ec_features.c
	gpio.c
	i2c_portmap.c
	md.c
	hibdelay.c
	hostevent.c
	panic_output.c
	port80.c
	powerindebug.c
	power_button.c
	pwr_avg.c
	rtc.c
	rw.c
	shared_mem.c
	sleepmask.c
	sleeptimeout.c
	switch.c
	sysinfo.c
	tcpci_dump.c
	usb_pd_console.c
	vboot_hash.c
	version.c
	waitms.c
)

# The upstream Host Command version doesn't support the hcdebug command
zephyr_library_sources_ifndef(CONFIG_EC_HOST_CMD hcdebug.c)
