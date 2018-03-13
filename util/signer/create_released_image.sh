#!/bin/bash

#
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# This script is a utility which allows to sign prod CR50 images for release
# and place them in a tarball suitable for uploading to the BCS.
#
# The util/signer/ec_RW-manifest-prod.json manifest present in the EC
# directory is used for signing.
#

set -u

# A very crude RO verification function. The key signature found at a fixed
# offset into the RO blob must match the RO type. Prod keys have bit D2 set to
# one, dev keys have this bit set to zero.
verify_ro() {
  local ro_bin="${1}"
  local type_expected="${2}"
  local key_byte

  if [ ! -f "${ro_bin}" ]; then
    echo "${ro_bin} not a file!" >&2
    exit 1
  fi

  # Key signature's lowest byte is byte #5 in the line at offset 0001a0.
  key_byte="$(od -Ax -t x1 -v "${ro_bin}" | awk '/0001a0/ {print $6};')"
  case "${key_byte}" in
    (?[4567cdef])
      if [ "${type_expected}" == "prod" ]; then
        return 0
      fi
      ;;
    (?[012389ab])
      if [ "${type_expected}" == "dev" ]; then
        return 0
      fi
      ;;
  esac

  echo "RO key in ${ro_bin} does not match type ${type_expected}" >&2
  exit 1
}

# This function prepares a full CR50 image, consisting of two ROs and two RWs
# placed at their respective offsets into the resulting blob. It invokes the
# bs (binary signer) script to actually convert elf versions of RWs into
# binaries and sign them.
#
# The signed image is placed in the directory named as concatenation of RO and
# RW version numbers and board ID fields, if set to non-default. The ebuild
# downloading the tarball from the BCS expects the image to be in that
# directory.
prepare_image() {
  local awk_prog
  local count=0
  local extra_param=
  local image_type="${1}"
  local raw_version
  local ro_a_hex="$(readlink -f "${2}")"
  local ro_b_hex="$(readlink -f "${3}")"
  local rw_a="$(readlink -f "${4}")"
  local rw_b="$(readlink -f "${5}")"
  local version

  for f in "${ro_a_hex}" "${ro_b_hex}"; do
    if ! objcopy -I ihex "${f}" -O binary "${TMPD}/${count}.bin"; then
      echo "failed to convert ${f} from hex to bin" >&2
      exit 1
    fi
    verify_ro "${TMPD}/${count}.bin" "${image_type}"
    : $(( count += 1 ))
  done

  if [ "${image_type}" == "prod" ]; then
    extra_param+=' prod'
  fi

  if ! "${EC_ROOT}/util/signer/bs" ${extra_param} elves \
    "${rw_a}" "${rw_b}" > /dev/null;
  then
    echo "Failed invoking ${EC_ROOT}/util/signer/bs ${extra_param} " \
      "elves ${rw_a} ${rw_b}" >&2
    exit 1
  fi

  dd if="${TMPD}/0.bin" of="${RESULT_FILE}" conv=notrunc
  dd if="${TMPD}/1.bin" of="${RESULT_FILE}" seek=262144 bs=1 conv=notrunc

  # A typical Cr50 version reported by gsctool looks as follows:
  # RO_A:0.0.10 RW_A:0.0.22[ABCD:00000013:00000012] ...(the same for R[OW]_B).
  #
  # In case Board ID field is not set in the image, it is reported as
  # [00000000:00000000:00000000]
  #
  # We want the generated tarball file name to include all relevant version
  # fields. Let's retrieve the version string and process it using awk to
  # generate the proper file name. Only the RO_A and RW_A version numbers are
  # used, this script trusts the user to submit for processing a proper image
  # where both ROs and both RWs are of the same version respectively.
  #
  # As a result, blob versions are converted as follows:
  #     RO_A:0.0.10 RW_A:0.0.22[ABCD:00000013:00000012] into
  #         r0.0.10.w0.0.22_ABCD_00000013_00000012
  #
  #     RO_A:0.0.10 RW_A:0.0.22[00000000:00000000:00000000] into
  #         r0.0.10.w0.0.22
  #
  # The below awk program accomplishes this preprocessing.
  awk_prog='/^RO_A:/ {
    # drop the RO_A/RW_A strings
    gsub(/R[OW]_A:/, "")
    # Drop default mask value completely.
    gsub(/\[00000000:00000000:00000000\]/, "")
    # If there is a non-default mask:
    # - replace opening brackets and colons with underscores.
    gsub(/[\[\:]/, "_")
    #  - drop the trailing bracket.
    gsub(/\]/, "")
    # Print filtered out RO_A and RW_A values
    print "r" $1 ".w" $2
}'

  raw_version="$("${GSCTOOL}" -b "${RESULT_FILE}")" ||
       ( echo "${ME}: Failed to retrieve blob version" >&2 && exit 1 )

  version="$(awk "${awk_prog}" <<< "${raw_version}" )"
  if [ -z "${dest_dir}" ]; then
    # Note that this is a global variable
    dest_dir="cr50.${version}"
    if [ ! -d "${dest_dir}" ]; then
      mkdir "${dest_dir}"
    else
      echo "${dest_dir} already exists, will overwrite" >&2
    fi
  elif [ "${dest_dir}" != "cr50.${version}" ]; then
    echo "dev and prod versions mismatch!" >&2
    exit 1
  fi

  cp "${RESULT_FILE}" "${dest_dir}/cr50.bin.${image_type}"
  echo "saved ${image_type} binary in ${dest_dir}/cr50.bin.${image_type}"
}

# Execution starts here ===========================
ME="$(basename $0)"

if [ -z "${CROS_WORKON_SRCROOT}" ]; then
 echo "${ME}: This script must run inside Chrome OS chroot" >&2
  exit 1
fi

SCRIPT_ROOT="${CROS_WORKON_SRCROOT}/src/scripts"
. "${SCRIPT_ROOT}/build_library/build_common.sh" || exit 1

TMPD="$(mktemp -d /tmp/${ME}.XXXXX)"
trap "/bin/rm -rf ${TMPD}" SIGINT SIGTERM EXIT

EC_ROOT="${CROS_WORKON_SRCROOT}/src/platform/ec"
RESULT_FILE="${TMPD}/release.bin"
dest_dir=
IMAGE_SIZE='524288'
export RESULT_FILE

GSCTOOL="/usr/sbin/gsctool"
if [[ ! -x "${GSCTOOL}" ]]; then
  emerge_command="USE=cr50_onboard sudo -E emerge ec-utils"
  echo "${ME}: gsctool not found, run \"${emerge_command}\"" >&2
  exit 1
fi

DEFINE_string cr50_board_id "" \
  "Optional string representing Board ID field of the Cr50 RW header.
Consists of three fields separated by colon: <RLZ>:<hex mask>:<hex flags>"

# Do not put this before the DEFINE_ invocations - they routinely experience
# error return values.
set -e

FLAGS_HELP="usage: ${ME} [flags] <blobs>

blobs are:
  <prod RO A>.hex <prod RO B>.hex  <RW.elf> <RW_B.elf>
 or
  <prod RO A>.hex <prod RO B>.hex  <dir>
    where <dir> contains files named ec.RW.elf and ec.RW_B.elf
"

# Parse command line.
FLAGS "$@" || exit 1

eval set -- "${FLAGS_ARGV}"

if [[ $# == 3  &&  -d "${3}" ]]; then
  rw_a="${3}/ec.RW.elf"
  rw_b="${3}/ec.RW_B.elf"
elif [[ $# == 4 ]]; then
  rw_a="${3}"
  rw_b="${4}"
else
  flags_help
  exit 1
fi

prod_ro_a="${1}"
prod_ro_b="${2}"

dd if=/dev/zero bs="${IMAGE_SIZE}" count=1  2>/dev/null |
  tr \\000 \\377 > "${RESULT_FILE}"
if [ "$(stat -c '%s' "${RESULT_FILE}")" != "${IMAGE_SIZE}" ]; then
  echo "Failed creating ${RESULT_FILE}" >&2
  exit 1
fi

# Used by the bs script.
export CR50_BOARD_ID="${FLAGS_cr50_board_id}"

prepare_image 'prod' "${prod_ro_a}" "${prod_ro_b}" "${rw_a}" "${rw_b}"
tarball="${dest_dir}.tbz2"
tar jcf  "${tarball}" "${dest_dir}"
rm -rf "${dest_dir}"

bcs_path="gs://chromeos-localmirror/distfiles"
echo "SUCCESS!!!!!!"
echo "use the below commands to copy the new image to the BCS"
echo "gsutil cp ${tarball} ${bcs_path}"
echo "gsutil acl ch -u AllUsers:R ${bcs_path}/${tarball}"
