#!/bin/bash
#
# Copyright 2017 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ec_commands_file_in="include/ec_commands.h"

#######################################
# Test if the following conditions hold for the ec host command
# The alpha numeric value of the define starts with 0x
# The alpha numeric value of the define is 4-hex digits
# The hex digits "A B C D E F" are capitalized
# Arguments:
#   string - ec host command to check
# Returns:
#   0 if command is ok, else 1
########################################
check_cmd() {
  IFS=" "
  # Remove any tabs that may exist
  tts=$(echo "$1" | sed 's/\t/ /g')
  arr=( $tts )

  # Check for 0x
  if [[ "${arr[2]}" != 0x* ]]; then
    return 1
  fi

  # Check that length is 6. 0x + 4 hex digits
  if [[ ${#arr[2]} != 6 ]]; then
    return 1
  fi

  # Check that hex digits are valid and uppercase
  hd=${arr[2]:2}
  if ! [[ $hd =~ ^[0-9A-F]{4}$ ]]; then
    return 1
  fi

  # command is ok
  return 0
}

#######################################
# Test if the string arg is in one of the following formats:
#  file.X:#define EC_CMD_X XxXXXX
#  file.X:#define EC_PRV_CMD_X XxXXXX
# Arguments:
#   string - potential ec host command
# Returns:
#   0 if command is formated properly, else 1
########################################
should_check() {
  IFS=" "
  arr=( $1 )

  # Check for file.X:#define
  IFS=":"
  temp=( ${arr[0]} )
  # Check for file.X
  if [ ! -f  "${temp[0]}" ]; then
    return 1
  fi

  # Check for #define
  if [[ "${temp[1]}" != "#define" ]]; then
    return 1
  fi

  # Check for EC_CMD_XXX or EC_PRV_CMD_XXX
  if [[ "${arr[1]}" != EC_CMD_* ]] && [[ "${arr[1]}" != EC_PRV_CMD_* ]]; then
    return 1
  fi

  # Check for EC_XXX_XXX(n)
  if [[ "${arr[1]}" =~ ')'$ ]]; then
    return 1
  fi

  return 0
}

main() {

  # Check if ec_commands.h has changed.
  echo ${PRESUBMIT_FILES} | grep -q "${ec_commands_file_in}" || exit 0

  ec_errors=()
  ei=0
  # Search all file occurrences of "EC_CMD" and store in array
  IFS=$'\n'
  ec_cmds=($(grep -H "EC_CMD" "${ec_commands_file_in}"))

  # Loop through and find valid occurrences of "EC_CMD" to check
  length=${#ec_cmds[@]}
  for ((i = 0; i != length; i++)); do
    if should_check "${ec_cmds[i]}"; then
      if ! check_cmd "${ec_cmds[i]}"; then
        ec_errors[$ei]="${ec_cmds[i]}"
        ((ei++))
      fi
    fi
  done

  # Search all file occurrances of "EC_PRV_CMD" and store in array
  IFS=$'\n'
  ec_prv_cmds=($(grep -H "EC_PRV_CMD" "${ec_commands_file_in}"))

  # Loop through and find valid occurrences of "EC_PRV_CMD" to check
  length=${#ec_prv_cmds[@]}
  for ((i = 0; i != length; i++)); do
    if should_check "${ec_prv_cmds[i]}"; then
      if ! check_cmd "${ec_prv_cmds[i]}"; then
        ec_errors[$ei]="${ec_prv_cmds[i]}"
        ((ei++))
      fi
    fi
  done

  # Check if any malformed ec host commands were found
  if [ ! $ei -eq 0 ]; then
    echo "The following host commands are malformed:"
    # print all malformed host commands
    for ((i = 0; i != ei; i++)); do
      echo "FILE: ${ec_errors[i]}"
    done
    exit 1
  fi

  exit 0
}

main "$@"
