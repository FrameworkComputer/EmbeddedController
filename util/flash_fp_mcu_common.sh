#!/bin/bash
# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

. /usr/share/misc/shflags

DEFINE_boolean 'read' "${FLAGS_FALSE}" 'Read instead of write' 'r'
FLAGS_HELP="Usage: ${0} [flags] ec.bin"

# Process commandline flags
FLAGS "${@}" || exit 1
eval set -- "${FLAGS_ARGV}"

if [[ "$#" -eq 0 ]]; then
  echo "Missing filename"
  flags_help
  exit 1
fi

# print out canonical path to differentiate between /usr/local/bin and
# /usr/bin installs
echo "$(readlink -f "$0")"

readonly CROS_EC_SPI_MODALIAS_STR="of:NcrfpTCgoogle,cros-ec-spi"

check_hardware_write_protect_disabled() {
  if ectool gpioget EC_WP_L | grep -q '= 0'; then
    echo "Please make sure WP is deasserted."
    exit 1
  fi
}

# Get the spiid for the fingerprint sensor based on the modalias
# string: https://crbug.com/955117
get_spiid() {
  for dev in /sys/bus/spi/devices/*; do
    if [[ "$(cat "${dev}/modalias")" == "${CROS_EC_SPI_MODALIAS_STR}" ]]; then
      echo "$(basename "${dev}")"
      exit 0
    fi
  done

  exit 1
}

flash_fp_mcu_stm32() {
  local spidev="${1}"
  local gpio_nrst="${2}"
  local gpio_boot0="${3}"
  local gpio_pwren="${4}"
  local file="${5}"
  local spiid

  local STM32MON_READ_FLAGS=" -U -u -p -s ${spidev} -r"
  local STM32MON_WRITE_FLAGS="-U -u -p -s ${spidev} -e -w"
  local stm32mon_flags=""

  if [[ "${FLAGS_read}" -eq "${FLAGS_TRUE}" ]]; then
    if [[ -e "${file}" ]]; then
      echo "Output file already exists: ${file}"
      exit 1
    fi
    stm32mon_flags="${STM32MON_READ_FLAGS}"
  else
    if [[ ! -f "${file}" ]]; then
      echo "Invalid image file: ${file}"
      exit 1
    fi
    stm32mon_flags="${STM32MON_WRITE_FLAGS}"
  fi

  check_hardware_write_protect_disabled

  spiid="$(get_spiid)"
  if [[ $? -ne 0 ]]; then
    echo "Unable to find FP sensor SPI device: ${CROS_EC_SPI_MODALIAS_STR}"
    exit 1
  fi

  echo "Flashing SPI device ID: ${spiid}"

  # Ensure the ACPI is not cutting power when unloading cros-ec-spi
  if [[ -n "${gpio_pwren}" ]]; then
    echo "${gpio_pwren}" > /sys/class/gpio/export
    echo "out" > "/sys/class/gpio/gpio${gpio_pwren}/direction"
    echo 1 > "/sys/class/gpio/gpio${gpio_pwren}/value"
  fi

  # Remove cros_fp if present
  echo "${spiid}" > /sys/bus/spi/drivers/cros-ec-spi/unbind

  # Configure the MCU Boot0 and NRST GPIOs
  echo "${gpio_boot0}" > /sys/class/gpio/export
  echo "out" > "/sys/class/gpio/gpio${gpio_boot0}/direction"
  echo "${gpio_nrst}" > /sys/class/gpio/export
  echo "out" > "/sys/class/gpio/gpio${gpio_nrst}/direction"

  # Reset sequence to enter bootloader mode
  echo 1 > "/sys/class/gpio/gpio${gpio_boot0}/value"
  echo 0 > "/sys/class/gpio/gpio${gpio_nrst}/value"
  sleep 0.001

  # load spidev (fail on cros-ec-spi first to change modalias)
  echo "${spiid}" > /sys/bus/spi/drivers/cros-ec-spi/bind 2>/dev/null
  echo "${spiid}" > /sys/bus/spi/drivers/spidev/bind

  # Release reset as the SPI bus is now ready
  echo 1 > "/sys/class/gpio/gpio${gpio_nrst}/value"
  echo "in" > "/sys/class/gpio/gpio${gpio_nrst}/direction"

  stm32mon ${stm32mon_flags} "${file}"

  # unload spidev
  echo "${spiid}" > /sys/bus/spi/drivers/spidev/unbind

  # Go back to normal mode
  echo "out" > "/sys/class/gpio/gpio${gpio_nrst}/direction"
  echo 0 > "/sys/class/gpio/gpio${gpio_boot0}/value"
  echo 0 > "/sys/class/gpio/gpio${gpio_nrst}/value"
  echo 1 > "/sys/class/gpio/gpio${gpio_nrst}/value"

  # Give up GPIO control
  echo "in" > "/sys/class/gpio/gpio${gpio_boot0}/direction"
  echo "in" > "/sys/class/gpio/gpio${gpio_nrst}/direction"
  echo "${gpio_boot0}" > /sys/class/gpio/unexport
  echo "${gpio_nrst}" > /sys/class/gpio/unexport

  # wait for FP MCU to come back up (including RWSIG delay)
  sleep 2
  # Put back cros_fp driver
  echo "${spiid}" > /sys/bus/spi/drivers/cros-ec-spi/bind
  # Kernel driver is back, we are no longer controlling power
  if [[ -n "${gpio_pwren}" ]]; then
    echo "${gpio_pwren}" > /sys/class/gpio/unexport
  fi
  # Test it
  ectool --name=cros_fp version
}
