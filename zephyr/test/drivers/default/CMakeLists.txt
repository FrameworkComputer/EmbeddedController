target_sources(app PRIVATE
    src/battery.c
    src/bb_retimer.c
    src/bma2x2.c
    src/bma4xx.c
    src/bmi_common.c
    src/bmi160.c
    src/bmi260.c
    src/charge_manager.c
    src/charge_ramp.c
    src/charge_state_prevent_power_on.c
    src/console.c
    src/cros_cbi.c
    src/espi.c
    src/gpio.c
    src/integration/usbc/usb.c
    src/integration/usbc/usb_20v_3a_pd_charger.c
    src/integration/usbc/usb_5v_3a_pd_sink.c
    src/integration/usbc/usb_5v_3a_pd_source.c
    src/integration/usbc/usb_attach_src_snk.c
    src/integration/usbc/usb_charging_policy.c
    src/integration/usbc/usb_hard_reset.c
    src/integration/usbc/usb_pd_bist_shared.c
    src/integration/usbc/usb_pd_ctrl_msg.c
    src/integration/usbc/usb_pd_rev3.c
    src/i2c.c
    src/i2c_passthru.c
    src/isl923x.c
    src/led.c
    src/lid_angle.c
    src/lid_switch.c
    src/lis2dw12.c
    src/ln9310.c
    src/locate_chip.c
    src/motion_sense/motion_sense.c
    src/panic.c
    src/panic_output.c
    src/port80.c
    src/power_common.c
    src/ppc_sn5s330.c
    src/ppc_syv682x.c
    src/ps8xxx.c
    src/stm_mems_common.c
    src/tablet_mode.c
    src/task.c
    src/tcpci.c
    src/tcpci_test_common.c
    src/tcpm_header.c
    src/tcs3400.c
    src/temp_sensor.c
    src/thermistor.c
    src/uart_hostcmd.c
    src/usb_mux.c
    src/util.c
    src/vboot_hash.c
    src/virtual_battery.c
    src/vstore.c
    src/watchdog.c
)

add_subdirectory(
    ${PLATFORM_EC}/zephyr/test/drivers/mock_panic_output
    mock_panic_output
)

# This test does not work when power functions are mocked, so only run it for
# the plain `drivers.default` test case.
zephyr_library_sources_ifndef(CONFIG_POWER_SEQUENCE_MOCK src/chipset.c)
