#!/bin/bash
#
# Copyright 2012 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Generate version information for the EC binary

: "${BOARD:=}"
: "${CR50_DEV:=}"
: "${CR50_SQA:=}"
: "${CRYPTO_TEST:=}"
: "${REPRODUCIBLE_BUILD:=}"
: "${STATIC_VERSION:=}"
: "${VCSID:=}"

# Use this symbol as a separator to be able to reliably concatenate strings of
# text.
dc=$'\001'

# Default marker to indicate 'dirty' repositories
dirty_marker='+'

# Derive path to chromeos_version.sh script
CHROOT_SOURCE_ROOT="/mnt/host/source"
CHROMIUMOS_OVERLAY="${CHROOT_SOURCE_ROOT}/src/third_party/chromiumos-overlay"
CROS_VERSION_SCRIPT="${CHROMIUMOS_OVERLAY}/chromeos/config/chromeos_version.sh"

# This function examines the state of the current directory and attempts to
# extract its version information: the latest tag, if any, how many patches
# are there since the latest tag, the top sha1, and if there are local
# modifications.
#
# Local modifications are reported by concatenating the revision string and
# the string '-dirty' using the $dc symbol as the separator.
#
# If there is no tags defined in this git repository, the base version is
# considered to be 0.0.
#
# If current directory is not a git depository, this function prints out
# "no_version"

get_tree_version() {
  local marker
  local ghash
  local numcommits
  local tag
  local vbase
  local ver_branch
  local ver_major

  if ghash="$(git rev-parse --short --verify HEAD 2>/dev/null)"; then
    if gdesc="$(git describe --dirty --match='v*' 2>/dev/null)"; then
      IFS="-" read -r -a fields <<< "${gdesc}"
      tag="${fields[0]}"
      IFS="." read -r -a vernum <<< "${tag}"
      numcommits=$((vernum[2]+${fields[1]:-0}))
      ver_major="${vernum[0]}"
      ver_branch="${vernum[1]}"
    else
      numcommits=$(git rev-list HEAD | wc -l)
      ver_major="v0"
      ver_branch="0"
    fi
    # avoid putting the -dirty attribute if only the timestamp
    # changed
    git status > /dev/null 2>&1

    if [ -n "$(git diff-index --name-only HEAD 2>/dev/null)" ]; then
      marker="${dirty_marker}"
    else
      marker="-"
    fi
    vbase="${ver_major}.${ver_branch}.${numcommits}${marker}${ghash}"
  else
    # Fall back to the VCSID provided by the packaging system if available.
    # Ex VCSID: 0.0.1-r1519-9b368af6a4943b90941471d0bdf7e7208788f898
    if [[ -n "${VCSID}" ]]; then
      ghash="${VCSID##*-}"
      vbase="v2.1.9999-${ghash:0:8}"
    else
      # then ultimately fails to "no_version"
      vbase="no_version"
    fi
  fi
  if [[ "${marker}" == "${dirty_marker}" ]]; then
      echo "${vbase}${dc}${marker}"
  else
      echo "${vbase}${dc}"
  fi
}


main() {
  local component
  local dir_list
  local gitdate
  local most_recent_file
  local most_recents
  local timestamp
  local tool_ver
  local values
  local vbase
  local ver

  if [[ -z "${STATIC_VERSION}" ]]; then
    ver="${CR50_SQA:+SQA/}${CR50_DEV:+DBG/}${CRYPTO_TEST:+CT/}${BOARD}_"
    tool_ver=""
  else
    ver="STATIC_VERSION"
    tool_ver="STATIC_VERSION_TOOL"
  fi
  most_recents=()    # Non empty if any of the component repos is 'dirty'.
  dir_list=( . )   # list of component directories, always includes the EC tree

  # Common/shared repos for fingerprint boards.
  local fp_common_dir_list=( )
  if [[ -d ../../third_party/boringssl ]]; then
    fp_common_dir_list+=( ../../third_party/boringssl )
  fi
  if [[ -d ../../third_party/cryptoc ]]; then
    fp_common_dir_list+=( ../../third_party/cryptoc )
  fi
  if [[ -d ../ec-private ]]; then
    fp_common_dir_list+=( ../ec-private )
  fi
  # Example: bloonchipper-druid
  if [[ -d ../ec-private/fingerprint/druid && "${BOARD}" =~ -druid$ ]]; then
    fp_common_dir_list+=( ../ec-private/fingerprint/druid  )
  fi

  case "${BOARD}" in
    cr50)
      dir_list+=( ../../third_party/tpm2 ../../third_party/cryptoc )
      ;;
    *_fp|*dartmonkey*|*bloonchipper*|helipilot*)
      dir_list+=( "${fp_common_dir_list[@]}" )
      if [[ -d ../fingerprint/fpc ]]; then
        dir_list+=( ../fingerprint/fpc )
      fi
      ;;
    buccaneer*)
      dir_list+=( "${fp_common_dir_list[@]}" )
      if [[ -d ../fingerprint/elan ]]; then
        dir_list+=( ../fingerprint/elan )
      fi
      ;;
    *_scp)
      if [[ -d ./private-mt-scp ]]; then
        dir_list+=( ./private-mt-scp )
      fi
      ;;
  esac

  # Create a combined version string for all component directories.
  if [[ -z "${STATIC_VERSION}" ]]; then
    for git_dir in "${dir_list[@]}"; do
      pushd "${git_dir}" > /dev/null || exit 1
      component="$(basename "${git_dir}")"
      IFS="${dc}" read -r -a values <<< "$(get_tree_version)"
      vbase="${values[0]}"             # Retrieved version information.
      if [[ -n "${values[1]}" ]]; then
        # From each modified repo get the most recently modified file.
        most_recent_file="$(git status --porcelain | \
                                 awk '$1 ~ /[M|A|?]/ {print $2}' | \
                                 xargs -r ls -t | head -1)"
        if [[ -n "${most_recent_file}" ]]; then
          most_recents+=("$(realpath "${most_recent_file}")")
        fi
      fi
      if [ "${component}" != "." ]; then
      ver+=" ${component}:"
      fi
      ver+="${vbase}"
      tool_ver+="${vbase}"

      if [[ "${git_dir}" == "." ]]; then
        # Truncate to 31 chars to leave room for terminating NUL that is
        # automatically added to constant C strings.
        ver_32="${ver:0:31}"
        ver="${ver_32}"
      fi

      popd > /dev/null || exit 1
    done
  fi

  echo "/* This file is generated by util/getversion.sh */"

  # Truncated version string that is exactly 32-bytes (NUL terminated).
  echo "#define CROS_EC_VERSION32 \"${ver_32}\""

  echo "/* Version string for ectool. */"
  echo "#define CROS_ECTOOL_VERSION \"${tool_ver}\""

  echo "/* Version string for stm32mon. */"
  echo "#define CROS_STM32MON_VERSION \"${tool_ver}\""

  echo "/* Sub-fields for use in Makefile.rules and to form build info string"
  echo " * in common/version.c. */"
  echo "#define VERSION \"${ver}\""
  if [[ -n "${STATIC_VERSION}" ]] || [[ "${REPRODUCIBLE_BUILD}" = 1 ]]; then
    echo '#define BUILDER "reproducible@build"'
  else
    echo "#define BUILDER \"${USER}@$(hostname)\""
  fi

  if [[ -n "${STATIC_VERSION}" ]]; then
    echo "#define DATE \"STATIC_VERSION_DATE\""
  elif [[ ${#most_recents[@]} != 0  ]]; then
    # There are modified files, use the timestamp of the most recent one as
    # the build version timestamp.
    # shellcheck disable=SC2012
    most_recent_file="$(ls -t "${most_recents[@]}"| head -1)"
    timestamp="$(stat -c '%y' "${most_recent_file}" | sed 's/\..*//')"
    echo "/* Repo is dirty, using time of most recent file modification. */"
    echo "#define DATE \"${timestamp}\""
  else
    echo "/* Repo is clean, use the commit date of the last commit. */"
    # If called from an ebuild we won't have a git repo, so redirect stderr
    # to avoid annoying 'Not a git repository' errors.
    gitdate="$(
      for git_dir in "${dir_list[@]}"; do
        git -C "${git_dir}" log -1 --format='%ct %ci' HEAD 2>/dev/null
      done | sort | tail -1 | cut -d ' ' -f '2 3')"
    echo "#define DATE \"${gitdate}\""
  fi

  # Use the chromeos_version_string when available.
  # This will not work if run from a standalone CrOS EC checkout.
  echo "#define CROS_FWID_MISSING_STR \"CROS_FWID_MISSING\""
  if [[ -f "${CROS_VERSION_SCRIPT}" ]]; then
    cros_version_output=$("${CROS_VERSION_SCRIPT}")
    CHROMEOS_BUILD=$(echo "${cros_version_output}" | \
                     grep "^ *CHROMEOS_BUILD=" | cut -d= -f2)
    CHROMEOS_BRANCH=$(echo "${cros_version_output}" | \
                      grep "^ *CHROMEOS_BRANCH=" | cut -d= -f2)
    CHROMEOS_PATCH=$(echo "${cros_version_output}" | \
                     grep "^ *CHROMEOS_PATCH=" | cut -d= -f2)
    # Official builds must set CHROMEOS_OFFICIAL=1.
    if [ "${CHROMEOS_OFFICIAL:-0}" -ne 1 ]; then
      # For developer builds, overwrite CHROMEOS_PATCH with the date.
      # This date is abbreviated compared to chromeos_version.sh so
      # fwid_version will more likely fit in 32 bytes.
      CHROMEOS_PATCH=$(date +%y_%m_%d)
    fi
    fwid="${BOARD}_${CHROMEOS_BUILD}.${CHROMEOS_BRANCH}.${CHROMEOS_PATCH}"
    echo "/* CrOS FWID of this build */"
    echo "#define CROS_FWID32 \"${fwid:0:31}\""
  else
    echo "/* CrOS FWID is not available for this build */"
    echo "#define CROS_FWID32 CROS_FWID_MISSING_STR"
  fi
}

main
